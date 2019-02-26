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

#include "src/tracing/inproc/inproc_shared_memory.h"

namespace perfetto {

InprocSharedMemory::InprocSharedMemory(size_t size)
    : mem_(base::PagedMemory::Allocate(size)), size_(size) {}

InprocSharedMemory::~InprocSharedMemory() = default;
InprocSharedMemory::Factory::~Factory() = default;

void* InprocSharedMemory::start() const {
  return mem_.Get();
}

size_t InprocSharedMemory::size() const {
  return size_;
}

std::unique_ptr<SharedMemory> InprocSharedMemory::Factory::CreateSharedMemory(
    size_t size) {
  return std::unique_ptr<SharedMemory>(new InprocSharedMemory(size));
}

}  // namespace perfetto
