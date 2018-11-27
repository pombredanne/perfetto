/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/profiling/memory/shared_ring_buffer.h"

#include <atomic>
#include <type_traits>

#include <inttypes.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/temp_file.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <linux/memfd.h>
#include <sys/syscall.h>
#endif

namespace perfetto {
namespace profiling {

namespace {
constexpr auto kMetaPageSize = base::kPageSize;

class ScopedSpinlock {
 public:
  ScopedSpinlock(std::atomic<bool>* lock) : lock_(lock) {
    if (PERFETTO_LIKELY(!(*lock_).exchange(true, std::memory_order_acquire))) {
      locked_ = true;
      return;
    }

    // Slowpath.
    for (size_t attempt = 0; attempt < 10000; attempt++) {
      if (!(*lock_).load(std::memory_order_relaxed) &&
          PERFETTO_LIKELY(
              !(*lock_).exchange(true, std::memory_order_acquire))) {
        locked_ = true;
        return;
      }
      if (attempt && attempt % 512 == 0)
        usleep(1000);
    }
  }

  ~ScopedSpinlock() {
    if (locked_) {
      PERFETTO_DCHECK((*lock_).load());
      (*lock_).store(false, std::memory_order_release);
    }
  }

  bool locked() const { return locked_; }

 private:
  std::atomic<bool>* const lock_;
  bool locked_ = false;
};

}  // namespace

SharedRingBuffer::SharedRingBuffer(CreateFlag, size_t size) {
  size_t size_with_meta = size + kMetaPageSize;
  // TODO(primiano): this is copy/pasted from posix_shared_memory.cc . Refactor.
  base::ScopedFile fd;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  bool is_memfd = false;
  fd.reset(static_cast<int>(syscall(__NR_memfd_create, "heaprofd_ringbuf",
                                    MFD_CLOEXEC | MFD_ALLOW_SEALING)));
  is_memfd = !!fd;

  if (!fd) {
    // TODO: if this fails on Android we should fall back on ashmem.
    PERFETTO_DPLOG("memfd_create() failed");
  }
#endif

  if (!fd)
    fd = base::TempFile::CreateUnlinked().ReleaseFD();

  PERFETTO_CHECK(fd);
  int res = ftruncate(fd.get(), static_cast<off_t>(size_with_meta));
  PERFETTO_CHECK(res == 0);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  if (is_memfd) {
    res = fcntl(*fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);
    PERFETTO_DCHECK(res == 0);
  }
#endif
  Initialize(std::move(fd));
}

SharedRingBuffer::SharedRingBuffer(AttachFlag, base::ScopedFile mem_fd) {
  Initialize(std::move(mem_fd));
}

SharedRingBuffer::~SharedRingBuffer() {
  static_assert(std::is_trivially_constructible<MetadataPage>::value,
                "MetadataPage must be trivially constructible");
  static_assert(std::is_trivially_destructible<MetadataPage>::value,
                "MetadataPage must be trivially destructible");

  if (is_valid())
    munmap(mem_, size_ * 2 + kMetaPageSize);
}

bool SharedRingBuffer::Write(const void* src, size_t size) {
  uint8_t* wr_ptr;
  {
    ScopedSpinlock try_spinlock(&meta_->spinlock);
    if (!try_spinlock.locked()) {
      // This is not really thread safe as the spinlock is not held, but it's
      // best-effort anyways.
      meta_->num_writes_failed++;
      return false;
    }

    if (IsCorrupt())
      return false;

    const uint64_t avail = size_ - (meta_->write_pos - meta_->read_pos);
    const size_t size_with_header = size + sizeof(uint32_t);
    if (size_with_header > avail) {
      meta_->num_writes_failed++;
      return false;
    }

    wr_ptr = mem_ + (meta_->write_pos & (size_ - 1));
    uint32_t size32 = static_cast<uint32_t>(size);
    memcpy(wr_ptr, &size32, sizeof(size32));
    wr_ptr += sizeof(size32);
    meta_->write_pos += size_with_header;
    meta_->bytes_written += size;
    meta_->num_writes_succeeded++;
    memcpy(wr_ptr, src, size);  // TODO: move out with acquire/release on size.
  }  // spinlock

  return true;
}

SharedRingBuffer::BufferAndSize SharedRingBuffer::Read() {
  ScopedSpinlock try_spinlock(&meta_->spinlock);
  if (!try_spinlock.locked()) {
    // This is not really thread safe as the spinlock is not held, but it's
    // best-effort anyways.
    meta_->num_reads_failed++;
    return BufferAndSize(nullptr, 0);
  }

  if (IsCorrupt()) {
    meta_->num_reads_failed++;
    return BufferAndSize(nullptr, 0);
  }

  if (meta_->write_pos - meta_->read_pos < sizeof(uint32_t))
    return BufferAndSize(nullptr, 0);  // No data

  uint8_t* rd_ptr = mem_ + (meta_->read_pos & (size_ - 1));
  uint32_t size;
  memcpy(&size, rd_ptr, sizeof(size));

  if (meta_->read_pos + sizeof(size) + size > meta_->write_pos) {
    PERFETTO_ELOG("Corrupted header detected, size=%" PRIu32 ", rd=%" PRIu64 ", wr=%" PRIu64,
                  size,  meta_->read_pos, meta_->write_pos);
    meta_->num_reads_failed++;
    return BufferAndSize(nullptr, 0);
  }

  rd_ptr += sizeof(size);
  BufferAndSize res{new uint8_t[size], size};
  memcpy(&res.first[0], rd_ptr, size);
  meta_->read_pos += sizeof(size) + size;
  return res;
}

bool SharedRingBuffer::IsCorrupt() {
  if (meta_->write_pos < meta_->read_pos ||
      meta_->write_pos - meta_->read_pos > size_) {
    PERFETTO_ELOG("Ring buffer corrupted, rd=%" PRIu64 ", wr=%" PRIu64
                  ", size=%zu",
                  meta_->read_pos, meta_->write_pos, size_);
    return true;
  }
  return false;
}

void SharedRingBuffer::Initialize(base::ScopedFile mem_fd) {
  struct stat stat_buf = {};
  int res = fstat(*mem_fd, &stat_buf);
  PERFETTO_CHECK(res == 0 && stat_buf.st_size > 0);
  auto size_with_meta = static_cast<size_t>(stat_buf.st_size);
  auto size = size_with_meta - kMetaPageSize;

  // |size_with_meta| must be a power of two number of pages + 1 page (for
  // metadata).
  if (size_with_meta < 2 * base::kPageSize ||
      size_with_meta % base::kPageSize || (size & (size - 1))) {
    PERFETTO_ELOG("SharedRingBuffer size is invalid (%zu)", size_with_meta);
    return;
  }

  // First of all reserve a virtual region of 2 x size.
  size_t outer_size = kMetaPageSize + size * 2;
  uint8_t* region = reinterpret_cast<uint8_t*>(
      mmap(nullptr, outer_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (region == MAP_FAILED) {
    PERFETTO_PLOG("mmap(PROT_NONE) failed");
    return;
  }

  // Map first the whole buffer (including the initial metadata page) @ off=0.
  void* reg1 = mmap(region, size_with_meta, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_FIXED, *mem_fd, 0);

  // When map again the buffer, skipping the metadta page. The final result is:
  // [ METADATA ] [ RING BUFFER SHMEM ] [ RING BUFFER SHMEM ]
  void* reg2 = mmap(region + size_with_meta, size, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_FIXED, *mem_fd,
                    /*offset=*/kMetaPageSize);

  if (reg1 != region || reg2 != region + size_with_meta) {
    PERFETTO_PLOG("mmap(MAP_SHARED) failed");
    munmap(region, outer_size);
    return;
  }
  meta_ = new (region) MetadataPage();
  size_ = size;
  mem_ = region + kMetaPageSize;
  mem_fd_ = std::move(mem_fd);
}

SharedRingBuffer::SharedRingBuffer(SharedRingBuffer&& other) noexcept {
  *this = std::move(other);
}

SharedRingBuffer& SharedRingBuffer::operator=(SharedRingBuffer&& other) {
  mem_fd_ = std::move(other.mem_fd_);
  std::tie(meta_, mem_, size_) = std::tie(other.meta_, other.mem_, other.size_);
  std::tie(other.meta_, other.mem_, other.size_) =
      std::make_tuple(nullptr, nullptr, 0);
  return *this;
}

// static
base::Optional<SharedRingBuffer> SharedRingBuffer::Create(size_t size) {
  auto buf = SharedRingBuffer(CreateFlag(), size);
  if (!buf.is_valid())
    return base::nullopt;
  return base::make_optional(std::move(buf));
}

// static
base::Optional<SharedRingBuffer> SharedRingBuffer::Attach(
    base::ScopedFile mem_fd) {
  auto buf = SharedRingBuffer(AttachFlag(), std::move(mem_fd));
  if (!buf.is_valid())
    return base::nullopt;
  return base::make_optional(std::move(buf));
}

}  // namespace profiling
}  // namespace perfetto
