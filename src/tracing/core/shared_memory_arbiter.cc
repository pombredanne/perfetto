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

#include "src/tracing/core/shared_memory_arbiter.h"

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "src/tracing/core/trace_writer_impl.h"

#include <limits>

namespace perfetto {

namespace {
constexpr size_t kMaxWriterID = SharedMemoryABI::kMaxWriterID;
constexpr auto kDefaultLayout = SharedMemoryABI::PageLayout::kPageDiv1;

WriterID NextID(WriterID id) {
  return id < kMaxWriterID ? id + 1 : 1;
}
}  // namespace

using Chunk = SharedMemoryABI::Chunk;

SharedMemoryArbiter::SharedMemoryArbiter(void* start,
                                         size_t size,
                                         size_t page_size,
                                         OnPageCompleteCallback callback,
                                         base::TaskRunner* task_runner)
    : shmem_(reinterpret_cast<uint8_t*>(start), size, page_size),
      on_page_complete_callback_(callback),
      task_runner_(task_runner) {}

Chunk SharedMemoryArbiter::GetNewChunk(
    const SharedMemoryABI::ChunkHeader& header,
    BufferID target_buffer,
    size_t size_hint) {
  PERFETTO_DCHECK(size_hint == 0);  // Not implemented yet.

  for (;;) {
    // TODO(primiano): Probably this lock is not really required and this code
    // could be rewritten leveraging only the Try* atomic operations in
    // SharedMemoryABI. But let's not be too adventurous for the moment.
    {
      std::lock_guard<std::mutex> scoped_lock(lock_);
      const size_t initial_page_idx = page_idx_;
      for (size_t i = 0; i < shmem_.num_pages(); i++) {
        page_idx_ = (initial_page_idx + i) % shmem_.num_pages();
        bool is_new_page = false;
        auto layout = kDefaultLayout;  // TODO(primiano): make dynamic.
        if (shmem_.is_page_free(page_idx_)) {
          // TODO(primiano): Use the |size_hint| here to decide the layout.
          is_new_page =
              shmem_.TryPartitionPage(page_idx_, layout, target_buffer);
        }
        uint32_t free_chunks;
        size_t tbuf;
        if (is_new_page) {
          free_chunks = (1 << SharedMemoryABI::kNumChunksForLayout[layout]) - 1;
          tbuf = target_buffer;
        } else {
          free_chunks = shmem_.GetFreeChunks(page_idx_);
          tbuf = shmem_.page_header(page_idx_)->target_buffer.load(
              std::memory_order_relaxed);
        }
        PERFETTO_DLOG("Free chunks for page %zu: %x. Target buffer: %zu",
                      page_idx_, free_chunks, tbuf);

        if (tbuf != target_buffer)
          continue;

        for (uint32_t chunk_idx = 0; free_chunks;
             chunk_idx++, free_chunks >>= 1) {
          if (!(free_chunks & 1))
            continue;
          // We found a free chunk.
          Chunk chunk = shmem_.TryAcquireChunkForWriting(page_idx_, chunk_idx,
                                                         tbuf, &header);
          if (!chunk.is_valid())
            continue;
          PERFETTO_DLOG("Acquired chunk %zu:%u", page_idx_, chunk_idx);
          return chunk;
        }
        // TODO: we should have some policy to guarantee fairness of the SMB
        // page allocator w.r.t |target_buffer|? Or is the SMB best-effort. All
        // chunk in the page are busy (either kBeingRead or kBeingWritten), or
        // all the pages are assigned to a different target buffer. Try with the
        // next page.
      }
    }  // std::lock_guard<std::mutex>
    // All chunks are taken (either kBeingWritten by us or kBeingRead by the
    // Service). TODO: at this point we should return a bankrupcy chunk, not
    // crash the process.
    PERFETTO_ELOG("Shared memory buffer overrun! Stalling");
    usleep(250000);
  }
}

void SharedMemoryArbiter::ReturnCompletedChunk(Chunk chunk) {
  bool should_post_callback = false;
  {
    std::lock_guard<std::mutex> scoped_lock(lock_);
    size_t page_index = shmem_.ReleaseChunkAsComplete(std::move(chunk));
    if (page_index != SharedMemoryABI::kInvalidPageIdx) {
      pages_to_notify_.push_back(static_cast<uint32_t>(page_index));
      if (!scheduled_notification_) {
        should_post_callback = true;
        scheduled_notification_ = true;
      }
    }
  }
  if (should_post_callback) {
    // TODO what happens if the arbiter gets destroyed?
    task_runner_->PostTask(
        std::bind(&SharedMemoryArbiter::InvokeOnPageCompleteCallback, this));
  }
}

// This is always invoked on the |task_runner_| thread.
void SharedMemoryArbiter::InvokeOnPageCompleteCallback() {
  std::vector<uint32_t> pages_to_notify;
  {
    std::lock_guard<std::mutex> scoped_lock(lock_);
    pages_to_notify = std::move(pages_to_notify_);
    pages_to_notify_.clear();
    scheduled_notification_ = false;
  }
  on_page_complete_callback_(pages_to_notify);
}

std::unique_ptr<TraceWriter> SharedMemoryArbiter::CreateTraceWriter(
    BufferID target_buffer) {
  return std::unique_ptr<TraceWriter>(
      new TraceWriterImpl(this, AcquireWriterID(), target_buffer));
}

WriterID SharedMemoryArbiter::AcquireWriterID() {
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

void SharedMemoryArbiter::ReleaseWriterID(WriterID id) {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  if (id >= active_writer_ids_.size() || !active_writer_ids_[id]) {
    PERFETTO_DCHECK(false);
    return;
  }
  active_writer_ids_[id] = false;
}

}  // namespace perfetto
