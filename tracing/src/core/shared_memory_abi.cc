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
#include "base/utils.h"

namespace perfetto {

namespace {
// Returns the largest 4-bytes aligned chunk size <= |page_size| / |divider|
// for each divider in PageLayout.
constexpr size_t GetChunkSize(size_t page_size, size_t divider) {
  return (page_size / divider) & ~3UL;
}

}  // namespace

SharedMemoryABI::SharedMemoryABI(void* start, size_t size, size_t page_size)
    : start_(reinterpret_cast<uintptr_t>(start)),
      size_(size),
      page_size_(page_size),
      num_pages_(size / page_size),
      chunk_sizes_(InitChunkSizes(page_size)) {
  static_assert(sizeof(PageHeader) == 8, "PageHeader size");
  static_assert(sizeof(ChunkHeader) == 8, "ChunkHeader size");
  static_assert(sizeof(ChunkHeader::PacketsState) == 4, "PacketsState size");

  static_assert(alignof(ChunkHeader) == PageHeader::kChunkAlignment,
                "ChunkHeader alignment");

  PERFETTO_CHECK(page_size >= 4096);
  PERFETTO_CHECK(page_size % 4096 == 0);
  PERFETTO_CHECK(page_size <= kMaxPageSize);
  PERFETTO_CHECK(size % page_size == 0);
}

// static
SharedMemoryABI::ChunkSizePerDivider SharedMemoryABI::InitChunkSizes(
    size_t page_size) {
  ChunkSizePerDivider res = {};
  for (size_t i = 0; i < base::ArraySize(kNumChunksPerLayout); i++) {
    size_t num_chunks = kNumChunksPerLayout[i];
    res[i] = num_chunks == 0 ? 0 : GetChunkSize(page_size, num_chunks);
  }
  return res;
}

void SharedMemoryABI::Chunk::Reset(uintptr_t begin, size_t size) {
  begin_ = reinterpret_cast<void*>(begin);
  end_ = reinterpret_cast<void*>(begin + size);
  PERFETTO_CHECK(begin % PageHeader::kChunkAlignment == 0);
  PERFETTO_CHECK(end_ >= begin_);
}

bool SharedMemoryABI::TryAcquireChunkForWriting(size_t page_idx,
                                                size_t chunk_idx,
                                                const ChunkHeader& header,
                                                Chunk* chunk) {
  PageHeader* page_header = GetPageHeader(page_idx);
  uint32_t state = page_header->layout.load(std::memory_order_relaxed);
  const size_t num_chunks = GetNumChunksInPage(state);

  // The page layout has changed (or the page is free).
  if (chunk_idx >= num_chunks)
    return false;

  // Somebody else took the chunk.
  if (((state >> (chunk_idx * PageHeader::kChunkShift)) &
       PageHeader::kChunkMask) != kChunkFree)
    return false;

  uint32_t next_state =
      state | (kChunkBeingWritten << (chunk_idx * PageHeader::kChunkShift));
  if (!page_header->layout.compare_exchange_weak(state, next_state,
                                                 std::memory_order_acq_rel)) {
    // TODO: returning here is too aggressive. We should look at the returned
    // |state| to figure out if somebody else took the same chunk (in which case
    // we should immediately return false) or if they took another chunk in the
    // same page (in which case we should just retry).
    return false;
  }

  uintptr_t page_start = start_ + page_idx * page_size_;
  const size_t chunk_size = GetChunkSizeForPage(state);
  uintptr_t chunk_offset_in_page = sizeof(PageHeader) + chunk_idx * chunk_size;
  chunk->Reset(page_start + chunk_offset_in_page, chunk_size);
  PERFETTO_DCHECK(chunk->end() <= end());
  ChunkHeader* new_header = chunk->header();
  new_header->packets.store(header.packets, std::memory_order_relaxed);
  new_header->identifier.store(header.identifier, std::memory_order_release);
  return true;
}

bool SharedMemoryABI::TryMarkAllChunksAsBeingRead(size_t page_idx) {
  PageHeader* page_header = GetPageHeader(page_idx);
  uint32_t state = page_header->layout.load(std::memory_order_relaxed);
  uint32_t next_state = state & PageHeader::kLayoutMask;
  for (size_t i = 0; i < kMaxChunksPerPage; i++) {
    uint32_t chunk_state = ((state >> (i * 2)) & PageHeader::kChunkMask);
    switch (chunk_state) {
      case kChunkFree:
      case kChunkComplete:
        next_state |= chunk_state << (i * 2);
        break;
      case kChunkBeingRead:
        // Only the Service can transition chunks into the kChunkBeingRead
        // state. This means that the Service is somehow trying to call this
        // method twice and is likely doing something wrong.
        PERFETTO_DCHECK(false);
      case kChunkBeingWritten:
        return false;
    }
  }
  // Rational for compare_exchange_weak (as opposite to _strong): once a chunk
  // is kChunkComplete, the Producer cannot move it back to any other state.
  // Similarly, only the Service can transition chunks into the kChunkFree
  // state. So, no ABA problem can happen, hence the _weak here.
  return page_header->layout.compare_exchange_weak(state, next_state,
                                                   std::memory_order_acq_rel);
}

bool SharedMemoryABI::TryPartitionPage(size_t page_idx, PageLayout layout) {
  uint32_t expected_state = 0;
  uint32_t next_state =
      (layout << PageHeader::kLayoutShift) & PageHeader::kLayoutMask;
  PageHeader* page_header = GetPageHeader(page_idx);
  return page_header->layout.compare_exchange_weak(expected_state, next_state,
                                                   std::memory_order_acq_rel);
}

size_t SharedMemoryABI::GetFreeChunks(size_t page_idx) {
  uint32_t layout =
      GetPageHeader(page_idx)->layout.load(std::memory_order_relaxed);
  const size_t num_chunks = GetNumChunksInPage(layout);
  size_t res = 0;
  for (size_t i = 0; i < num_chunks; i++) {
    res |= ((layout & PageHeader::kChunkMask) == kChunkFree) ? 1 : 0;
    res <<= 1;
    layout >>= PageHeader::kChunkShift;
  }
  return res;
}

void SharedMemoryABI::MarkChunkAsComplete(Chunk chunk) {
  size_t page_idx;
  size_t chunk_idx;
  std::tie(page_idx, chunk_idx) = GetPageAndChunkIndex(chunk);

  for (int attempt = 0; attempt < 64; attempt++) {
    PageHeader* page_header = GetPageHeader(page_idx);
    uint32_t state = page_header->layout.load(std::memory_order_relaxed);
    const size_t page_chunk_size = GetChunkSizeForPage(state);
    PERFETTO_CHECK(chunk.size() == page_chunk_size);
    const uint32_t chunk_state =
        ((state >> (chunk_idx * PageHeader::kChunkShift)) &
         PageHeader::kAllChunksMask);
    PERFETTO_CHECK(chunk_state == kChunkBeingWritten);
    uint32_t next_state = state & ~(PageHeader::kChunkMask
                                    << (chunk_idx * PageHeader::kChunkShift));
    next_state |= kChunkComplete << (chunk_idx * PageHeader::kChunkShift);
    if (page_header->layout.compare_exchange_weak(state, next_state,
                                                  std::memory_order_acq_rel)) {
      return;
    }
    std::this_thread::yield();
  }
  // Too much contention on this page. Give up. This page will be left pending
  // forever but there isn't much more we can do at this point.
  PERFETTO_DCHECK(false);
}

uint16_t SharedMemoryABI::Chunk::GetPacketCount() const {
  return header()->packets.load(std::memory_order_acquire).count;
}

void SharedMemoryABI::Chunk::IncreasePacketCount(bool last_packet_is_partial) {
  // A chunk state is supposed to be modified only by the Producer and only by
  // one thread. There is no need of CAS here (if the caller behaves properly).
  ChunkHeader* chunk_header = header();
  auto state = chunk_header->packets.load(std::memory_order_relaxed);
  state.count++;
  if (last_packet_is_partial)
    state.flags |= ChunkHeader::PacketsState::kLastPacketContinuesOnNextChunk;

  // This needs to be a release store because if the Service sees this, it also
  // has to be guaranteed to see all the previous stores for the protobuf packet
  // bytes.
  chunk_header->packets.store(state, std::memory_order_release);
}

std::pair<size_t, size_t> SharedMemoryABI::GetPageAndChunkIndex(Chunk chunk) {
  PERFETTO_CHECK(chunk.is_valid());
  PERFETTO_CHECK(chunk.begin_addr() >= start_);
  PERFETTO_CHECK(chunk.end_addr() <= start_ + size_);

  // TODO: this could be optimized if we cache |page_shift_|.
  const size_t page_idx = chunk.begin_addr() / page_size_;
  const size_t offset = chunk.begin_addr() % page_size_;
  PERFETTO_CHECK(offset >= sizeof(PageHeader));
  PERFETTO_CHECK(offset % PageHeader::kChunkAlignment == 0);
  PERFETTO_CHECK((offset - sizeof(PageHeader)) % chunk.size() == 0);
  const size_t chunk_idx = (offset - sizeof(PageHeader)) / chunk.size();
  PERFETTO_CHECK(chunk_idx < kMaxChunksPerPage);
  return std::make_pair(page_idx, chunk_idx);
}

}  // namespace perfetto
