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

#include "src/profiling/memory/client.h"

#include <inttypes.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>

#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"
#include "src/profiling/memory/transport_data.h"

namespace perfetto {
namespace {

std::atomic<uint64_t> global_sequence_number(0);
constexpr size_t kFreePageBytes = base::kPageSize;
constexpr size_t kFreePageSize = kFreePageBytes / sizeof(uint64_t);

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
// glibc does not define a wrapper around gettid, bionic does.
pid_t gettid() {
  return static_cast<pid_t>(syscall(__NR_gettid));
}
#endif

#if defined(__arm__)
constexpr size_t kRegisterDataSize =
    unwindstack::ARM_REG_LAST * sizeof(uint32_t);
#elif defined(__aarch64__)
constexpr size_t kRegisterDataSize =
    unwindstack::ARM64_REG_LAST * sizeof(uint64_t);
#elif defined(__i386__)
constexpr size_t kRegisterDataSize =
    unwindstack::X86_REG_LAST * sizeof(uint32_t);
#elif defined(__x86_64__)
constexpr size_t kRegisterDataSize =
    unwindstack::X86_64_REG_LAST * sizeof(uint64_t);
#elif defined(__mips__) && !defined(__LP64__)
constexpr size_t kRegisterDataSize =
    unwindstack::MIPS_REG_LAST * sizeof(uint32_t);
#elif defined(__mips__) && defined(__LP64__)
constexpr size_t kRegisterDataSize =
    unwindstack::MIPS64_REG_LAST * sizeof(uint64_t);
#else
#error "Could not determine register data size"
#endif
}  // namespace

}  // namespace

FreePage::FreePage() : free_page_(kFreePageSize) {
  free_page_[0] = static_cast<uint64_t>(kFreePageBytes);
  free_page_[1] = static_cast<uint64_t>(RecordType::Free);
  offset_ = 2;
  // Code in Add assumes that offset is aligned to 2.
  PERFETTO_DCHECK(offset_ % 2 == 0);
}

void FreePage::Add(const void* addr, SocketPool* pool) {
  std::lock_guard<std::mutex> l(mtx_);
  if (offset_ == kFreePageSize)
    Flush(pool);
  static_assert(kFreePageSize % 2 == 0,
                "free page size needs to be divisible by two");
  free_page_[offset_++] = reinterpret_cast<uint64_t>(++global_sequence_number);
  free_page_[offset_++] = reinterpret_cast<uint64_t>(addr);
  PERFETTO_DCHECK(offset_ % 2 == 0);
}

void FreePage::Flush(SocketPool* pool) {
  BorrowedSocket fd(pool->Borrow());
  size_t written = 0;
  do {
    ssize_t wr = PERFETTO_EINTR(send(*fd, &free_page_[0] + written,
                                     kFreePageBytes - written, MSG_NOSIGNAL));
    if (wr == -1) {
      fd.Close();
      return;
    }
    written += static_cast<size_t>(wr);
  } while (written < kFreePageBytes);
  // Now that we have flushed, reset to after the header.
  offset_ = 2;
}

BorrowedSocket::BorrowedSocket(base::ScopedFile fd, SocketPool* socket_pool)
    : fd_(std::move(fd)), socket_pool_(socket_pool) {}

int BorrowedSocket::operator*() {
  return get();
}

int BorrowedSocket::get() {
  return *fd_;
}

void BorrowedSocket::Close() {
  fd_.reset();
}

BorrowedSocket::~BorrowedSocket() {
  if (socket_pool_ != nullptr)
    socket_pool_->Return(std::move(fd_));
}

SocketPool::SocketPool(std::vector<base::ScopedFile> sockets)
    : sockets_(std::move(sockets)), available_sockets_(sockets_.size()) {}

BorrowedSocket SocketPool::Borrow() {
  std::unique_lock<std::mutex> lck_(mtx_);
  if (available_sockets_ == 0)
    cv_.wait(lck_, [this] { return available_sockets_ > 0; });
  PERFETTO_CHECK(available_sockets_ > 0);
  return {std::move(sockets_[--available_sockets_]), this};
}

void SocketPool::Return(base::ScopedFile sock) {
  if (!sock)
    return;
  std::unique_lock<std::mutex> lck_(mtx_);
  PERFETTO_CHECK(available_sockets_ < sockets_.size());
  sockets_[available_sockets_++] = std::move(sock);
  if (available_sockets_ == 1) {
    lck_.unlock();
    cv_.notify_one();
  }
}

uint8_t* GetThreadStackBase() {
  pthread_t t = pthread_self();
  pthread_attr_t attr;
  if (pthread_getattr_np(t, &attr) != 0) {
    return nullptr;
  }
  uint8_t* x;
  size_t s;
  if (pthread_attr_getstack(&attr, reinterpret_cast<void**>(&x), &s) != 0)
    return nullptr;
  pthread_attr_destroy(&attr);
  return x + s;
}

// Bionic currently does not cache the addresses of the stack for the main
// thread.
// https://android.googlesource.com/platform/bionic/+/32bc0fcf69dfccb3726fe572833a38b01179580e/libc/bionic/pthread_attr.cpp#254
uint8_t* GetMainThreadStackBase() {
  FILE* maps = fopen("/proc/self/maps", "r");
  if (maps == nullptr) {
    PERFETTO_ELOG("heapprofd fopen failed %d", errno);
    return nullptr;
  }
  while (!feof(maps)) {
    char line[1024];
    char* data = fgets(line, sizeof(line), maps);
    if (data != nullptr && strstr(data, "[stack]")) {
      char* sep = strstr(data, "-");
      if (sep == nullptr)
        continue;
      sep++;
      fclose(maps);
      return reinterpret_cast<uint8_t*>(strtoll(sep, nullptr, 16));
    }
  }
  fclose(maps);
  PERFETTO_ELOG("heapprofd reading stack failed");
  return nullptr;
}

Client::Client() : main_thread_stack_base_(GetMainThreadStackBase()) {}

uint8_t* Client::GetStackBase() {
  if (gettid() == getpid())
    return main_thread_stack_base_;
  return GetThreadStackBase();
}

void Client::SendStack(uint64_t alloc_size, uint64_t alloc_address) {
  uint8_t* stacktop = __builtin_frame_address(0);
  uint8_t* stackbase = GetStackBase();
  uint8_t reg_buffer[kRegisterDataSize];
  unwindstack::AsmGetRegs(reg_buffer);
  AllocMetadata metadata;
  const size_t stack_size = static_cast<size_t>(stackbase - stacktop);
  const uint64_t total_size =
      sizeof(AllocMetadata) + kRegisterDataSize + stack_size;
  struct iovec iov[3];
  iov[0].iov_base = &total_size;
  iov[0].iov_size = sizeof(uint64_t);
  iov[1].iov_base = &metadata;
  iov[1].iov_size = sizeof(metadata);
  iov[2].iov_base = stacktop;
  iov[2].iov_size = stack_size;
  struct msghdr hdr;
  hdr.msg_iov = iov;
  hdr.msg_iovlen = base::ArraySize(iov);
  PERFETTO_CHECK(sendmsg(
}

}  // namespace perfetto
