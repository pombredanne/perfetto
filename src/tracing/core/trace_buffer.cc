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

#include "src/tracing/core/trace_buffer.h"

#include <limits>

#include "perfetto/base/logging.h"
#include "perfetto/tracing/core/shared_memory_abi.h"

namespace perfetto {

namespace {
constexpr size_t kMaxChunkRecordSize =
    0xffff - sizeof(SharedMemoryABI::PageHeader);
}  // namespace.

TraceBuffez::TraceBuffez() {
  // See comments in ChunkRecord for the rationale of this.
  static_assert(sizeof(ChunkRecord) == sizeof(SharedMemoryABI::PageHeader) +
                                           sizeof(SharedMemoryABI::ChunkHeader),
                "ChunkRecord out of sync with the layout of SharedMemoryABI");
}

TraceBuffez::TraceBuffez(TraceBuffez&&) noexcept = default;
TraceBuffez& TraceBuffez::operator=(TraceBuffez&&) = default;

bool TraceBuffez::Create(size_t size) {
  // The buffer should be at least as big to fit the biggest possible chunk.
  if (size < kMaxChunkRecordSize || size % base::kPageSize) {
    PERFETTO_ELOG("Invalid buffer size %zu", size);
    return false;
  }

  data_ = base::PageAllocator::AllocateMayFail(size);
  if (!data_) {
    PERFETTO_ELOG("Trace buffer allocation failed (size: %zu)", size);
    return false;
  }
  size_ = size;
  ClearContentsAndResetRWCursors();
  return true;
}

void TraceBuffez::ClearContentsAndResetRWCursors() {
  // HMM maybe rely on zeroes? Or pre-fault? Who knows.
  wptr_ = rptr_ = begin();
  while (writable_size()) {
    AddPaddingRecord(std::min(writable_size(), kMaxChunkRecordSize));
  }
  wptr_ = rptr_ = begin();
}

// Note: |src| points to a shmem region that is shared with the producer. Assume
// that the producer is malicious and will change the content of [src, src+size]
// while we execute here. Don't do any processing on |src| other than memcpy()
// or reading atomic words at fixed positions.
void TraceBuffez::CopyChunk(uint16_t producer_id,
                            uint16_t writer_id,
                            uint16_t chunk_id,
                            const uint8_t* payload,
                            size_t payload_size) {
  PERFETTO_DCHECK(writer_id != ChunkRecord::kWriterIdPadding);

  // Ensure that we never end up in a fragmented state where available_size()
  // is > 0 but < sizeof(ChunkRecord).
  size_t rounded_size =
      base::Align<sizeof(ChunkRecord)>(payload_size + sizeof(ChunkRecord));

  ChunkRecord record{};
  static_assert(
      kMaxChunkRecordSize <= std::numeric_limits<decltype(record.size)>::max(),
      "kMaxChunkRecordSize too big");

  if (PERFETTO_UNLIKELY(rounded_size > kMaxChunkRecordSize)) {
    PERFETTO_DCHECK(false);
    return;
  }

  // If there isn't enough room from the given write position, write a padding
  // record to clear the trailing remainder of the buffer and wrap back.
  if (PERFETTO_UNLIKELY(rounded_size > writable_size())) {
    AddPaddingRecord(writable_size());  // Takes care of wrapping |wptr_|.
    PERFETTO_DCHECK(rounded_size <= writable_size());
  }

  record.size = static_cast<decltype(record.size)>(rounded_size);
  record.chunk_id = chunk_id;
  record.writer_id = writer_id;
  record.producer_id = producer_id;

  size_t chunk_size = ReadChunkRecordSizeAt(wptr_);
  PERFETTO_DCHECK(chunk_size > 0 && !(chunk_size & (sizeof(ChunkRecord) - 1)));
  DcheckIsAlignedAndWithinBounds(wptr_);
  if (chunk_size == 0) {
    // The buffer is clear and this is the first time we are writing a record in
    // this region of the buffer.
    WriteChunkRecord(record, payload, payload_size);
  } else {
    // We are overwriting another (or more than one) ChunkRecord. In this case
    // we need to first clear up the next ChunkRecord(s) to keep the buffer
    // consistent. e.g. (in the example below, (w) == write cursor):
    //
    // Initial state:
    // (w)
    // |0        |10               |30                  |50
    // +---------+-----------------+--------------------+--------------------+
    // | Chunk 1 | Chunk 2         | Chunk 3            | Chunk 4            |
    // +---------+-----------------+--------------------+--------------------+
    //
    // Let's assume we now want now write a 5th Chunk of size == 35, the final
    // state should look like this:
    //                                   (w)
    // |0                                |35             |50
    // +---------------------------------+---------------+--------------------+
    // | Chunk 5                         | Padding Chunk | Chunk 4            |
    // +---------------------------------+---------------+--------------------+
    //

    // Find the starting position of the first chunk which begins at or after
    // |wptr_| + new chunk size (Chunk 4 in the example above). Note that such a
    // chunk might not exist and we might reach the end of the buffer.
    // Once found, write a padding chunk exactly at:
    // (position found) - (end of new chunk, that is |wptr_| + |rounded_size|).
    uint8_t* next_chunk = wptr_;
    for (;;) {
      size_t next_chunk_size = ReadChunkRecordSizeAt(next_chunk);

      // We should never hit this, unless we managed to screw up while writing
      // to the buffer and breaking the ChunkRecord(s) chain.
      if (PERFETTO_UNLIKELY(next_chunk_size + next_chunk > end())) {
        PERFETTO_DCHECK(false);
        PERFETTO_ELOG("TraceBuffer corruption detected. Clearing buffer");
        ClearContentsAndResetRWCursors();
        return;
      }

      if ((next_chunk - wptr_) + next_chunk_size >= rounded_size)
        break;

      next_chunk += next_chunk_size;
    }

    WriteChunkRecord(record, payload, payload_size);
    PERFETTO_DCHECK(next_chunk >= wptr_ && next_chunk <= end());

    // TODO: add test for the case of: wptr @ Chunk4, New chunk size >>
    // remainder (to force wrapping) but identical to Chunk 1.

    if (next_chunk > wptr_)
      AddPaddingRecord(next_chunk - wptr_);
  }
}

void TraceBuffez::AddPaddingRecord(size_t size) {
  PERFETTO_DCHECK(size <= kMaxChunkRecordSize);
  ChunkRecord record{};
  record.size = static_cast<decltype(record.size)>(size);
  record.writer_id = ChunkRecord::kWriterIdPadding;
  WriteChunkRecord(record, nullptr, 0);
}

}  // namespace perfetto
