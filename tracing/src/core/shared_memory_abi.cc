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

#include "tracing/core/shared_memory_abi.h"

#include "base/logging.h"

namespace perfetto {

namespace {
// Returns the largest 4-bytes aligned chunk size <= |page_size| / |divider|
// for each divider in PageLayout.
constexpr size_t GetChunkSize(size_t page_size, size_t divider) {
  return (page_size / divider) & ~3UL;
}

}  // namespace

SharedMemoryABI::SharedMemoryABI(void* start, size_t size, size_t page_size)
    : start_(reinterpret_cast<uintptr_t>(start),
      size_(size),
      page_size_(page_size),
      num_pages_(size / page_size),
      chunk_sizes_(InitChunkSizes(page_size)) {
  static_assert(sizeof(SharedMemoryABI::PageHeader) == 8, "PageHeader size");
  static_assert(sizeof(SharedMemoryABI::ChunkHeader) == 8, "ChunkHeader size");

  PERFETTO_CHECK(page_size >= 4096);
  PERFETTO_CHECK(page_size % 4096 == 0);
  PERFETTO_CHECK(size % page_size == 0);
}

// static
SharedMemoryABI::ChunkSizePerDivider SharedMemoryABI::InitChunkSizes(
    size_t page_size) {
  ChunkSizePerDivider res;
  res[PageLayout::kPageFree] = 0;
  res[PageLayout::kPageReserved1] = GetChunkSize(page_size, 1);
  ;
  res[PageLayout::kPageReserved2] = GetChunkSize(page_size, 1);
  ;
  res[PageLayout::kPageDiv1] = GetChunkSize(page_size, 1);
  res[PageLayout::kPageDiv2] = GetChunkSize(page_size, 2);
  res[PageLayout::kPageDiv4] = GetChunkSize(page_size, 4);
  res[PageLayout::kPageDiv7] = GetChunkSize(page_size, 7);
  res[PageLayout::kPageDiv14] = GetChunkSize(page_size, 14);
  return res;
}

ChunkHeader& GetChunkHeader(size_t chunk_index);

}  // namespace perfetto
