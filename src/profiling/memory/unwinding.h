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

#ifndef SRC_PROFILING_MEMORY_UNWINDING_H_
#define SRC_PROFILING_MEMORY_UNWINDING_H_

#include "perfetto/base/build_config.h"

#include <unwindstack/Maps.h>
#include <unwindstack/Unwinder.h>

#include "perfetto/base/scoped_file.h"
#include "perfetto/base/thread_task_runner.h"
#include "perfetto/tracing/core/basic_types.h"
#include "src/profiling/memory/bookkeeping.h"
#include "src/profiling/memory/unwound_messages.h"
#include "src/profiling/memory/wire_protocol.h"

namespace perfetto {
namespace profiling {

// Read /proc/[pid]/maps from an open file descriptor.
// TODO(fmayer): Figure out deduplication to other maps.
class FileDescriptorMaps : public unwindstack::Maps {
 public:
  FileDescriptorMaps(base::ScopedFile fd);

  FileDescriptorMaps(const FileDescriptorMaps&) = delete;
  FileDescriptorMaps& operator=(const FileDescriptorMaps&) = delete;

  FileDescriptorMaps(FileDescriptorMaps&& m) : Maps(std::move(m)) {
    fd_ = std::move(m.fd_);
  }

  FileDescriptorMaps& operator=(FileDescriptorMaps&& m) {
    if (&m != this)
      fd_ = std::move(m.fd_);
    Maps::operator=(std::move(m));
    return *this;
  }

  virtual ~FileDescriptorMaps() override = default;

  bool Parse() override;
  void Reset();

 private:
  base::ScopedFile fd_;
};

class FDMemory : public unwindstack::Memory {
 public:
  FDMemory(base::ScopedFile mem_fd);
  size_t Read(uint64_t addr, void* dst, size_t size) override;

 private:
  base::ScopedFile mem_fd_;
};

// Overlays size bytes pointed to by stack for addresses in [sp, sp + size).
// Addresses outside of that range are read from mem_fd, which should be an fd
// that opened /proc/[pid]/mem.
class StackOverlayMemory : public unwindstack::Memory {
 public:
  StackOverlayMemory(std::shared_ptr<unwindstack::Memory> mem,
                     uint64_t sp,
                     uint8_t* stack,
                     size_t size);
  StackOverlayMemory(std::shared_ptr<unwindstack::Memory> mem)
      : StackOverlayMemory(std::move(mem), 0u, nullptr, 0u) {}

  size_t Read(uint64_t addr, void* dst, size_t size) override;

  void SetStack(uint64_t sp, uint8_t* stack, size_t size);

 private:
  std::shared_ptr<unwindstack::Memory> mem_;
  uint64_t sp_ = 0;
  uint64_t stack_end_ = 0;
  uint8_t* stack_ = nullptr;
};

struct UnwindingMetadata {
  UnwindingMetadata(pid_t p, base::ScopedFile maps_fd, base::ScopedFile mem);

  void ReparseMaps();

  pid_t pid;
  std::unique_ptr<FileDescriptorMaps> maps;
  // The API of libunwindstack expects shared_ptr for Memory.
  std::shared_ptr<StackOverlayMemory> fd_mem;
  unwindstack::Unwinder unwinder;
};

bool DoUnwind(WireMessage*, UnwindingMetadata* metadata, AllocRecord* out);

class UnwindingWorker : public base::UnixSocket::EventListener {
 public:
  class Delegate {
   public:
    virtual void PostAllocRecord(AllocRecord) = 0;
    virtual void PostFreeRecord(FreeRecord) = 0;
    virtual void PostSocketDisconnected(DataSourceInstanceID, pid_t pid) = 0;
    virtual ~Delegate();
  };

  struct HandoffData {
    DataSourceInstanceID data_source_instance_id;
    base::UnixSocketRaw sock;
    base::ScopedFile fds[kHandshakeSize];
    SharedRingBuffer shmem;
  };

  UnwindingWorker(Delegate* delegate, base::ThreadTaskRunner thread_task_runner)
      : delegate_(delegate),
        thread_task_runner_(std::move(thread_task_runner)) {}

  // Public API safe to call from other threads.
  void PostDisconnectSocket(pid_t pid);
  void PostHandoffSocket(HandoffData);

  // Implementation of UnixSocket::EventListener.
  // Do not call explicitly.
  void OnDisconnect(base::UnixSocket* self) override;
  void OnNewIncomingConnection(base::UnixSocket*,
                               std::unique_ptr<base::UnixSocket>) override {
    PERFETTO_DFATAL("This should not happen.");
  }
  void OnDataAvailable(base::UnixSocket* self) override;

 public:
  // static and public for testing/fuzzing
  static void HandleBuffer(const SharedRingBuffer::Buffer& buf,
                           UnwindingMetadata* unwinding_metadata,
                           DataSourceInstanceID data_source_instance_id,
                           pid_t peer_pid,
                           Delegate* delegate);

 private:
  void HandleHandoffSocket(HandoffData data);
  void HandleDisconnectSocket(pid_t pid);

  struct ClientData {
    DataSourceInstanceID data_source_instance_id;
    std::unique_ptr<base::UnixSocket> sock;
    UnwindingMetadata metadata;
    SharedRingBuffer shmem;
  };

  std::map<pid_t, ClientData> client_data_;
  Delegate* delegate_;
  // Task runner with a dedicated thread.
  base::ThreadTaskRunner thread_task_runner_;
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_UNWINDING_H_
