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
constexpr auto kAlignment = 8;  // 64 bits to use aligned memcpy().
constexpr auto kHeaderSize = kAlignment;
constexpr auto kGuardSize = base::kPageSize * 1024 * 16;  // 64 MB.

}  // namespace

ScopedSpinlock::ScopedSpinlock(std::atomic<bool>* lock, Mode mode)
    : lock_(lock) {
  if (PERFETTO_LIKELY(!lock_->exchange(true, std::memory_order_acquire))) {
    locked_ = true;
    return;
  }

  // Slowpath.
  for (size_t attempt = 0; mode == Mode::Blocking || attempt < 1024 * 10;
       attempt++) {
    if (!lock_->load(std::memory_order_relaxed) &&
        PERFETTO_LIKELY(!lock_->exchange(true, std::memory_order_acquire))) {
      locked_ = true;
      return;
    }
    if (attempt && attempt % 1024 == 0)
      usleep(1000);
  }
}

ScopedSpinlock::ScopedSpinlock(ScopedSpinlock&& other)
    : lock_(other.lock_), locked_(other.locked_) {
  other.locked_ = false;
}

ScopedSpinlock& ScopedSpinlock::operator=(ScopedSpinlock&& other) {
  using std::swap;
  ScopedSpinlock tmp(std::move(other));
  swap(*this, tmp);
  return *this;
}

ScopedSpinlock::~ScopedSpinlock() {
  Unlock();
}

void swap(ScopedSpinlock& a, ScopedSpinlock& b) {
  std::swap(a.lock_, b.lock_);
  std::swap(a.locked_, b.locked_);
}

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

SharedRingBuffer::~SharedRingBuffer() {
  static_assert(std::is_trivially_constructible<MetadataPage>::value,
                "MetadataPage must be trivially constructible");
  static_assert(std::is_trivially_destructible<MetadataPage>::value,
                "MetadataPage must be trivially destructible");

  if (is_valid()) {
    size_t outer_size = kMetaPageSize + size_ * 2 + kGuardSize;
    munmap(meta_, outer_size);
  }
}

void SharedRingBuffer::Initialize(base::ScopedFile mem_fd) {
  struct stat stat_buf = {};
  int res = fstat(*mem_fd, &stat_buf);
  if (res != 0 && stat_buf.st_size == 0) {
    PERFETTO_PLOG("Could not attach to fd.");
    return;
  }
  auto size_with_meta = static_cast<size_t>(stat_buf.st_size);
  auto size = size_with_meta - kMetaPageSize;

  // |size_with_meta| must be a power of two number of pages + 1 page (for
  // metadata).
  if (size_with_meta < 2 * base::kPageSize || size % base::kPageSize ||
      (size & (size - 1))) {
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    PERFETTO_ELOG("SharedRingBuffer size is invalid (%zu)", size_with_meta);
#endif
    return;
  }

  // First of all reserve the whole virtual region to fit the buffer twice
  // + metadata page + red zone at the end.
  size_t outer_size = kMetaPageSize + size * 2 + kGuardSize;
  uint8_t* region = reinterpret_cast<uint8_t*>(
      mmap(nullptr, outer_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (region == MAP_FAILED) {
    PERFETTO_PLOG("mmap(PROT_NONE) failed");
    return;
  }

  // Map first the whole buffer (including the initial metadata page) @ off=0.
  void* reg1 = mmap(region, size_with_meta, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_FIXED, *mem_fd, 0);

  // Then map again the buffer, skipping the metadata page. The final result is:
  // [ METADATA ] [ RING BUFFER SHMEM ] [ RING BUFFER SHMEM ]
  void* reg2 = mmap(region + size_with_meta, size, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_FIXED, *mem_fd,
                    /*offset=*/kMetaPageSize);

  if (reg1 != region || reg2 != region + size_with_meta) {
    PERFETTO_PLOG("mmap(MAP_SHARED) failed");
    munmap(region, outer_size);
    return;
  }
  size_ = size;
  meta_ = new (region) MetadataPage();
  mem_ = region + kMetaPageSize;
  mem_end_ = region + size_with_meta + size;
  mem_fd_ = std::move(mem_fd);
}

SharedRingBuffer::WriteBuffer SharedRingBuffer::BeginWrite(
    const ScopedSpinlock& spinlock,
    size_t size) {
  PERFETTO_DCHECK(spinlock.locked());
  WriteBuffer result;

  if (IsCorrupt())
    return result;

  result.size_with_header_ = base::AlignUp<kAlignment>(size + kHeaderSize);
  if (result.size_with_header_ > write_avail(spinlock)) {
    meta_->num_writes_failed++;
    return result;
  }

  result.size_ = size;
  result.write_pos_ = meta_->write_pos;
  result.wr_ptr_ = at(meta_->write_pos);
  meta_->write_pos += result.size_with_header_;
  meta_->bytes_written += size;
  meta_->num_writes_succeeded++;
  // By making this an release store, we can save grabbing the spinlock in
  // EndWrite.
  reinterpret_cast<std::atomic<uint32_t>*>(result.wr_ptr_)
      ->store(0, std::memory_order_release);
  return result;
}

void SharedRingBuffer::EndWrite(const WriteBuffer& buf) {
  reinterpret_cast<std::atomic<uint32_t>*>(buf.wr_ptr_)
      ->store(static_cast<uint32_t>(buf.size_), std::memory_order_release);
}

SharedRingBuffer::ReadBuffer SharedRingBuffer::Read() {
  ScopedSpinlock spinlock(&meta_->spinlock, ScopedSpinlock::Mode::Blocking);

  if (IsCorrupt()) {
    meta_->num_reads_failed++;
    return ReadBuffer();
  }

  if (read_avail(spinlock) < kHeaderSize)
    return ReadBuffer();  // No data

  uint8_t* rd_ptr = at(meta_->read_pos);
  size_t size = reinterpret_cast<std::atomic<uint32_t>*>(rd_ptr)->load(
      std::memory_order_acquire);
  if (size == 0)
    return ReadBuffer();
  const size_t size_with_header = base::AlignUp<kAlignment>(size + kHeaderSize);

  if (size_with_header > read_avail(spinlock) ||
      rd_ptr + size_with_header >= mem_end_) {
    PERFETTO_ELOG(
        "Corrupted header detected, size=%zu"
        ", read_avail=%zu, rd=%" PRIu64 ", wr=%" PRIu64,
        size, read_avail(spinlock), meta_->read_pos, meta_->write_pos);
    meta_->num_reads_failed++;
    return ReadBuffer();
  }

  rd_ptr += kHeaderSize;
  return ReadBuffer(rd_ptr, size, size_with_header, this);
}

void SharedRingBuffer::EndRead(const ReadBuffer& buf) {
  ScopedSpinlock spinlock(&meta_->spinlock, ScopedSpinlock::Mode::Blocking);
  meta_->read_pos += buf.size_with_header_;
}

bool SharedRingBuffer::IsCorrupt() {
  if (meta_->write_pos < meta_->read_pos ||
      meta_->write_pos - meta_->read_pos > size_ ||
      meta_->write_pos % kAlignment || meta_->read_pos % kAlignment) {
    PERFETTO_ELOG("Ring buffer corrupted, rd=%" PRIu64 ", wr=%" PRIu64
                  ", size=%zu",
                  meta_->read_pos, meta_->write_pos, size_);
    return true;
  }
  return false;
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

SharedRingBuffer::ReadBuffer::~ReadBuffer() {
  if (ring_buffer_)
    ring_buffer_->EndRead(*this);
}

SharedRingBuffer::ReadBuffer::ReadBuffer(ReadBuffer&& other) noexcept
    : data_(other.data_),
      size_(other.size_),
      size_with_header_(other.size_with_header_),
      ring_buffer_(other.ring_buffer_) {
  other.ring_buffer_ = nullptr;
}

SharedRingBuffer::ReadBuffer& SharedRingBuffer::ReadBuffer::operator=(
    ReadBuffer&& other) noexcept {
  ReadBuffer tmp(std::move(other));
  using std::swap;
  swap(*this, tmp);
  return *this;
}

uint8_t* SharedRingBuffer::WriteBuffer::buf() {
  return wr_ptr_ + kHeaderSize;
}

void swap(SharedRingBuffer::ReadBuffer& a, SharedRingBuffer::ReadBuffer& b) {
  using std::swap;
  swap(a.data_, b.data_);
  swap(a.size_, b.size_);
  swap(a.size_with_header_, b.size_with_header_);
  swap(a.ring_buffer_, b.ring_buffer_);
}

}  // namespace profiling
}  // namespace perfetto
