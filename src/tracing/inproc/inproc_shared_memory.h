/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACING_INPROC_INPROC_SHARED_MEMORY_H_
#define SRC_TRACING_INPROC_INPROC_SHARED_MEMORY_H_

#include <stddef.h>

#include <memory>

#include "perfetto/base/paged_memory.h"
#include "perfetto/tracing/core/shared_memory.h"

namespace perfetto {

// A SharedMemory implementation for in-process use cases. Essentially a wrapper
// around aligned-malloc, as memory is shared by design within a process.
class InprocSharedMemory : public SharedMemory {
 public:
  class Factory : public SharedMemory::Factory {
   public:
    ~Factory() override;
    std::unique_ptr<SharedMemory> CreateSharedMemory(size_t size) override;
  };

  explicit InprocSharedMemory(size_t size);
  ~InprocSharedMemory() override;

  void* start() const override;
  size_t size() const override;

 private:
  base::PagedMemory mem_;
  size_t size_ = 0;
};

}  // namespace perfetto

#endif  // SRC_TRACING_INPROC_INPROC_SHARED_MEMORY_H_
