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

#include "tracing/src/core/producer_shared_memory_arbiter.h"

#include "base/logging.h"
#include "tracing/src/core/trace_writer_impl.h"

#include <limits>

namespace perfetto {

namespace {
static constexpr size_t kMaxWriterID = SharedMemoryABI::kMaxWriterID;

WriterID NextID(WriterID id) {
  return id < kMaxWriterID ? id + 1 : 1;
}
}  // namespace

using Chunk = SharedMemoryABI::Chunk;

ProducerSharedMemoryArbiter::ProducerSharedMemoryArbiter(void* start,
                                                         size_t size,
                                                         size_t page_size)
    : shmem_(start, size, page_size) {}

Chunk ProducerSharedMemoryArbiter::GetNewChunk(
    const SharedMemoryABI::ChunkHeader& header,
    size_t size_hint) {
  PERFETTO_DCHECK(size_hint == 0);  // Not implemented yet.

  // TODO(primiano): I start suspecting that this lock is not really required
  // and this code could be rewritten leveraging purely the Try* atomic
  // operations in SharedMemoryABI. Let's not be too adventurous for the moment.
  std::lock_guard<std::mutex> scoped_lock(lock_);

  const size_t initial_page_idx = page_idx_;
  do {
    if (shmem_.is_page_free(page_idx_)) {
      // TODO: Use the |size_hint| here to decide the layout.
      auto layout = SharedMemoryABI::PageLayout::kPageDiv4;
      shmem_.TryPartitionPage(page_idx_, layout, 0 /* target_buffer */);
    }
    // At this point either the page has been just partitioned or was already
    // partitioned. TODO: this code could be optimized using the return value of
    // TryPartitionPage() above.
    uint32_t free_chunks = shmem_.GetFreeChunks(page_idx_);
    PERFETTO_DLOG("Free chunks for page %zu: %x", page_idx_, free_chunks);

    for (uint32_t chunk_idx = 0; free_chunks; chunk_idx++, free_chunks >>= 1) {
      if (free_chunks & 1) {
        // We found a free chunk.
        Chunk chunk =
            shmem_.TryAcquireChunkForWriting(page_idx_, chunk_idx, &header);
        if (chunk.is_valid()) {
          PERFETTO_DCHECK(chunk.is_valid());
          PERFETTO_DLOG("Acquired chunk %zu:%u", page_idx_, chunk_idx);
          return chunk;
        }
      }
    }
    // All chunk in the page are busy (either kBeingRead or kBeingWritten).
    // Try with the next page.
    page_idx_ = (page_idx_ + 1) % shmem_.num_pages();
  } while (page_idx_ != initial_page_idx);

  // All chunks are taken (either kBeingWritten by us or kBeingRead by the
  // Service). TODO: at this point we should return a bankrupcy chunk, not
  // crash the process.
  PERFETTO_CHECK(false);
}

void ProducerSharedMemoryArbiter::ReturnCompletedChunk(Chunk chunk) {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  shmem_.ReleaseChunkAsComplete(std::move(chunk));
}

std::unique_ptr<TraceWriter> ProducerSharedMemoryArbiter::CreateTraceWriter(
    uint32_t target_buffer) {
  return std::unique_ptr<TraceWriter>(
      new TraceWriterImpl(this, AcquireWriterID(), target_buffer));
}

WriterID ProducerSharedMemoryArbiter::AcquireWriterID() {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  for (size_t i = 0; i < kMaxWriterID; i++) {
    last_writer_id_ = NextID(last_writer_id_);
    const WriterID id = last_writer_id_;

    // 0 is never a valid ID. So if we are looking for |id| == N and there are
    // N or less elements in the vector, they must necessarily be all < N.
    // e.g. if |id| == 4 and size() == 4, the vector will contain IDs 0,1,2,3.
    if (id >= active_writer_ids_.size()) {
      active_writer_ids_.resize(id + 1);
      active_writer_ids_[id] = true;
      return id;
    }

    if (!active_writer_ids_[id]) {
      active_writer_ids_[id] = true;
      return id;
    }
  }
  PERFETTO_DCHECK(false);
  return 0;
}

void ProducerSharedMemoryArbiter::ReleaseWriterID(WriterID id) {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  if (id >= active_writer_ids_.size() || !active_writer_ids_[id]) {
    PERFETTO_DCHECK(false);
    return;
  }
  active_writer_ids_[id] = false;
}

}  // namespace perfetto
