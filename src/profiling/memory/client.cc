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

std::vector<base::ScopedFile> ConnectPool(const std::string& sock_name,
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
      PERFETTO_PLOG("Failed to connect to %s", sock_name.c_str());
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

inline bool IsMainThread() {
  return getpid() == gettid();
}

}  // namespace

FreePage::FreePage() {
  FreePageHeader* header = reinterpret_cast<FreePageHeader*>(&free_page_[0]);
  header->size = sizeof(free_page_) - sizeof(uint64_t);
  header->record_type = RecordType::Free;
  offset_ = 1;
}

void FreePage::Add(const uint64_t addr,
                   const uint64_t sequence_number,
                   SocketPool* pool) {
  std::lock_guard<std::mutex> l(mutex_);
  if (offset_ == kFreePageSize)
    FlushLocked(pool);
  FreePageEntry& current_entry = free_page_[offset_++];
  current_entry.sequence_number = sequence_number;
  current_entry.addr = addr;
}

void FreePage::FlushLocked(SocketPool* pool) {
  BorrowedSocket fd(pool->Borrow());
  size_t written = 0;
  // TODO(fmayer): Add timeout.
  do {
    ssize_t wr =
        PERFETTO_EINTR(send(*fd, &free_page_[0] + written,
                            sizeof(free_page_) - written, MSG_NOSIGNAL));
    if (wr == -1) {
      fd.Close();
      return;
    }
    written += static_cast<size_t>(wr);
  } while (written < sizeof(free_page_));
  // Now that we have flushed, reset to after the header.
  offset_ = 1;
}



SocketPool::SocketPool(std::vector<base::ScopedFile> sockets)
    : sockets_(std::move(sockets)), available_sockets_(sockets_.size()) {}

BorrowedSocket SocketPool::Borrow() {
  std::unique_lock<std::mutex> lck_(mutex_);
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
  std::unique_lock<std::mutex> lck_(mutex_);
  PERFETTO_CHECK(available_sockets_ < sockets_.size());
  sockets_[available_sockets_++] = std::move(sock);
  if (available_sockets_ == 1) {
    lck_.unlock();
    cv_.notify_one();
  }
}

const char* GetThreadStackBase() {
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

Client::Client(std::vector<base::ScopedFile> socks)
    : socket_pool_(std::move(socks)) {
  uint64_t size = 0;
  int fds[2];
  fds[0] = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  fds[1] = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
  base::Send(*socket_pool_.Borrow(), &size, sizeof(size), fds, 2);
}

Client::Client(const std::string& sock_name, size_t conns)
    : Client(ConnectPool(sock_name, conns)) {}

const char* Client::GetStackBase() {
  if (IsMainThread()) {
    if (!main_thread_stack_base_)
      // Because pthread_attr_getstack reads and parses /proc/self/maps and
      // /proc/self/stat, we have to cache the result here.
      main_thread_stack_base_ = GetThreadStackBase();
    return main_thread_stack_base_;
  }
  return GetThreadStackBase();
}

void Client::RecordMalloc(uint64_t alloc_size, uint64_t alloc_address) {
  const char* stacktop = reinterpret_cast<char*>(__builtin_frame_address(0));
  const char* stackbase = GetStackBase();
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
