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

#ifndef INCLUDE_PERFETTO_BASE_PAGE_ALLOCATOR_H_
#define INCLUDE_PERFETTO_BASE_PAGE_ALLOCATOR_H_

#include <memory>

namespace perfetto {
namespace base {

class PageAllocator {
 public:
  class Deleter {
   public:
    Deleter();
    explicit Deleter(size_t);
    void operator()(void*) const;

   private:
    size_t size_;
  };

  // Used only as a marker for Allocate() below.
  struct Unchecked {};
  static Unchecked unchecked;

  using UniquePtr = std::unique_ptr<void, Deleter>;

  // Allocates |size| bytes using mmap(MAP_ANONYMOUS). The returned pointer is
  // guaranteed to be page-aligned and the memory is guaranteed to be zeroed.
  // |size| must be a multiple of 4KB (a page size).
  // The default version will crash if the mmap() fails. The unchecked overload,
  // instead, will return a nullptr in case of failure.
  static UniquePtr Allocate(size_t size);
  static UniquePtr Allocate(size_t size, const Unchecked&);
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_PAGE_ALLOCATOR_H_
