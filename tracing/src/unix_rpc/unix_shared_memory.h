/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef TRACING_SRC_POSIX_IPC_POSIX_SHARED_MEMORY_H_
#define TRACING_SRC_POSIX_IPC_POSIX_SHARED_MEMORY_H_

#include <stddef.h>

#include <memory>

#include "tracing/core/shared_memory.h"

namespace perfetto {

class UnixSharedMemory : public SharedMemory {
 public:
  class Factory : public SharedMemory::Factory {
   public:
    ~Factory() override;
    std::unique_ptr<SharedMemory> CreateSharedMemory(size_t) override;
  };

  // Create a brand new SHM region (the service uses this).
  static std::unique_ptr<UnixSharedMemory> Create(size_t size);

  // Mmaps a file descriptor to an existing SHM region (the producer uses this).
  static std::unique_ptr<UnixSharedMemory> AttachToFd(int fd);

  ~UnixSharedMemory() override;

  int fd() const { return fd_; }

  // SharedMemory implementation.
  void* start() const override { return start_; }
  size_t size() const override { return size_; }

 private:
  static std::unique_ptr<UnixSharedMemory> MapFD(int fd, size_t size);

  UnixSharedMemory(void* start, size_t size, int fd);
  UnixSharedMemory(const UnixSharedMemory&) = delete;
  UnixSharedMemory& operator=(const UnixSharedMemory&) = delete;

  void* const start_;
  const size_t size_;
  int fd_;
};

}  // namespace perfetto

#endif  // TRACING_SRC_POSIX_IPC_POSIX_SHARED_MEMORY_H_
