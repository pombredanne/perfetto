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
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <new>

#include <unwindstack/MachineArm.h>
#include <unwindstack/MachineArm64.h>
#include <unwindstack/MachineMips.h>
#include <unwindstack/MachineMips64.h>
#include <unwindstack/MachineX86.h>
#include <unwindstack/MachineX86_64.h>
#include <unwindstack/Regs.h>
#include <unwindstack/RegsGetLocal.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/sock_utils.h"
#include "perfetto/base/utils.h"
#include "src/profiling/memory/transport_data.h"

namespace perfetto {
namespace {

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
#else
#error "Could not determine register data size"
#endif

std::vector<base::ScopedFile> MultipleConnect(const std::string& sock_name,
                                              size_t n) {
  sockaddr_un addr;
  socklen_t addr_size;
  if (!base::MakeSockAddr(sock_name, &addr, &addr_size))
    return {};

  std::vector<base::ScopedFile> res;
  res.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    auto sock = base::CreateSocket();
    if (connect(*sock, reinterpret_cast<sockaddr*>(&addr), addr_size) == -1)
      PERFETTO_ELOG("Failed to connect to %s", sock_name.c_str());
    res.emplace_back(std::move(sock));
  }
  return res;
}

#pragma pack(1)
struct StackHeader {
  uint64_t record_size;
  RecordType type;
  AllocMetadata alloc_metadata;
  char register_data[kRegisterDataSize];
};

}  // namespace

FreePage::FreePage() : free_page_(kFreePageSize) {
  free_page_[0] = static_cast<uint64_t>(kFreePageBytes);
  free_page_[1] = static_cast<uint64_t>(RecordType::Free);
  offset_ = 2;
  // Code in Add assumes that offset is aligned to 2.
  PERFETTO_DCHECK(offset_ % 2 == 0);
}

void FreePage::Add(const uint64_t addr,
                   const uint64_t sequence_number,
                   SocketPool* pool) {
  std::lock_guard<std::mutex> l(mtx_);
  if (offset_ == kFreePageSize)
    Flush(pool);
  static_assert(kFreePageSize % 2 == 0,
                "free page size needs to be divisible by two");
  free_page_[offset_++] = sequence_number;
  free_page_[offset_++] = addr;
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
  if (!sock) {
    // TODO(fmayer): Handle reconnect or similar.
    // This is just to prevent a deadlock.
    PERFETTO_CHECK(++dead_sockets_ != sockets_.size());
    return;
  }
  std::unique_lock<std::mutex> lck_(mtx_);
  PERFETTO_CHECK(available_sockets_ < sockets_.size());
  sockets_[available_sockets_++] = std::move(sock);
  if (available_sockets_ == 1) {
    lck_.unlock();
    cv_.notify_one();
  }
}

char* GetThreadStackBase() {
  // Not called from main thread.
  PERFETTO_DCHECK(gettid() != getpid());
  pthread_t t = pthread_self();
  pthread_attr_t attr;
  if (pthread_getattr_np(t, &attr) != 0)
    return nullptr;
  base::ScopedResource<pthread_attr_t*, pthread_attr_destroy, nullptr> cleanup(
      &attr);

  char* stackaddr;
  size_t stacksize;
  if (pthread_attr_getstack(&attr, reinterpret_cast<void**>(&stackaddr),
                            &stacksize) != 0)
    return nullptr;
  return stackaddr + stacksize;
}

// Bionic currently does not cache the addresses of the stack for the main
// thread.
// https://android.googlesource.com/platform/bionic/+/32bc0fcf69dfccb3726fe572833a38b01179580e/libc/bionic/pthread_attr.cpp#254
// TODO(fmayer): Figure out with bionic if caching in bionic makes sense.
char* GetMainThreadStackBase() {
  base::ScopedFstream maps(fopen("/proc/self/maps", "r"));
  if (!maps) {
    PERFETTO_ELOG("heapprofd fopen failed %d", errno);
    return nullptr;
  }
  while (!feof(*maps)) {
    char line[1024];
    char* data = fgets(line, sizeof(line), *maps);
    if (data != nullptr && strstr(data, "[stack]")) {
      char* sep = strstr(data, "-");
      if (sep == nullptr)
        continue;
      sep++;
      return reinterpret_cast<char*>(strtoll(sep, nullptr, 16));
    }
  }
  PERFETTO_ELOG("heapprofd reading stack failed");
  return nullptr;
}

Client::Client(std::vector<base::ScopedFile> socks)
    : socket_pool_(std::move(socks)),
      main_thread_stack_base_(GetMainThreadStackBase()) {
  uint64_t size = 0;
  int fds[2];
  fds[0] = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  fds[1] = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
  base::Send(*socket_pool_.Borrow(), &size, sizeof(size), fds, 2);
}

Client::Client(const std::string& sock_name, size_t conns)
    : Client(MultipleConnect(sock_name, conns)) {}

char* Client::GetStackBase() {
  if (gettid() == getpid())
    return main_thread_stack_base_;
  return GetThreadStackBase();
}

void Client::RecordMalloc(uint64_t alloc_size, uint64_t alloc_address) {
  char* stacktop = reinterpret_cast<char*>(__builtin_frame_address(0));
  char* stackbase = GetStackBase();
  char reg_data[kRegisterDataSize];
  unwindstack::AsmGetRegs(reg_data);

  if (stackbase < stacktop) {
    PERFETTO_DCHECK(false);
    return;
  }
  char* data = reinterpret_cast<char*>(alloca(sizeof(StackHeader)));
  size_t total_size = static_cast<size_t>(stackbase - data);

  StackHeader* stack_header = new (data) StackHeader;
  stack_header->record_size = total_size - sizeof(stack_header->record_size);
  stack_header->type = RecordType::Malloc;
  AllocMetadata& metadata = stack_header->alloc_metadata;
  metadata.alloc_size = alloc_size;
  metadata.alloc_address = alloc_address;
  metadata.stack_pointer = reinterpret_cast<uint64_t>(stacktop);
  PERFETTO_DCHECK(stacktop > data);
  metadata.stack_pointer_offset = static_cast<uint64_t>(stacktop - data);
  metadata.arch = unwindstack::Regs::CurrentArch();
  metadata.sequence_number = ++sequence_number_;

  memcpy(&stack_header->register_data, reg_data, sizeof(reg_data));

  BorrowedSocket sockfd = socket_pool_.Borrow();
  PERFETTO_CHECK(
      PERFETTO_EINTR(send(*sockfd, data, total_size, MSG_NOSIGNAL)) ==
      static_cast<ssize_t>(total_size));
}

void Client::RecordFree(uint64_t alloc_address) {
  free_page_.Add(alloc_address, ++sequence_number_, &socket_pool_);
}

}  // namespace perfetto
