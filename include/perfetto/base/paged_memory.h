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

#ifndef INCLUDE_PERFETTO_BASE_PAGED_MEMORY_H_
#define INCLUDE_PERFETTO_BASE_PAGED_MEMORY_H_

#include <memory>

namespace perfetto {
namespace base {

class PagedMemory {
 public:
  // Initializes an invalid PagedMemory pointing to nullptr.
  PagedMemory();

  PagedMemory(PagedMemory&& other);
  PagedMemory& operator=(PagedMemory&& other);

  // Allocates |size| bytes using mmap(MAP_ANONYMOUS). The returned memory is
  // guaranteed to be page-aligned and guaranteed to be zeroed. |size| must be a
  // multiple of 4KB (a page size). Crashes if the underlying mmap() fails. When
  // |commit| is true, the memory is immediately committed. Otherwise, the
  // memory may only be reserved and the user should call EnsureCommitted()
  // before writing to memory addresses.
  static PagedMemory Allocate(size_t size, bool commit);

  // Like the above, but returns a PagedMemory pointing to nullptr if the mmap()
  // fails (e.g., if out of virtual address space).
  static PagedMemory AllocateMayFail(size_t size, bool commit);

  ~PagedMemory();

  // Hint to the OS that the memory range is not needed and can be discarded.
  // The memory remains accessible and its contents may be retained, or they
  // may be zeroed. This function may be a NOP on some platforms. Returns true
  // if implemented.
  bool AdviseDontNeed(void* p, size_t size);

  // Ensures that the memory region up to but excluding |p| is committed. The
  // implementation may commit memory in larger chunks above and beyond |p| to
  // minimize the number of commits. Returns |false| if the memory couldn't be
  // committed.
  bool EnsureCommitted(void* p);

  void* get() const noexcept;
  explicit operator bool() const noexcept;

 private:
  static PagedMemory AllocateInternal(size_t size, bool commit, bool unchecked);

  PagedMemory(char* p, size_t size, bool commit_all);
  PagedMemory(const PagedMemory&) = delete;
  PagedMemory& operator=(const PagedMemory&) = delete;

  char* p_;
  size_t size_;
  size_t committed_size_ = 0u;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_PAGED_MEMORY_H_
