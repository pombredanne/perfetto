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

#include <sys/mman.h>
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
  madvise(begin(), size_, MADV_DONTNEED);
  wptr_ = begin();
  index_.clear();
  last_chunk_id_.clear();
}

// Note: |src| points to a shmem region that is shared with the producer. Assume
// that the producer is malicious and will change the content of [src, src+size]
// while we execute here. Don't do any processing on |src| other than memcpy()
// or reading atomic words at fixed positions.
void TraceBuffez::CopyChunkFromUntrustedShmem(ProducerID producer_id,
                                              WriterID writer_id,
                                              ChunkID chunk_id,
                                              uint8_t num_packets,
                                              uint8_t flags,
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
  record.num_packets = num_packets;

  // At this point either |wptr_| points to an untouched part of the buffer
  // (i.e. *wptr_ == 0) or we are about to overwrite one or more ChunkRecord(s).
  // In this case we need to first figure out where the next valid ChunkRecord
  // is going to be (if exists) and add padding between the new record and the
  // latter.
  // Example ((w) == write cursor):
  //
  // Initial state:
  // |0 (w)    |10               |30                  |50
  // +---------+-----------------+--------------------+--------------------+
  // | Chunk 1 | Chunk 2         | Chunk 3            | Chunk 4            |
  // +---------+-----------------+--------------------+--------------------+
  //
  // Let's assume we now want now write a 5th Chunk of size == 35, the final
  // state should look like this:
  // |0                                |35 (w)         |50
  // +---------------------------------+---------------+--------------------+
  // | Chunk 5                         | Padding Chunk | Chunk 4            |
  // +---------------------------------+---------------+--------------------+
  //

  // Find the position of the first chunk which begins at or after
  // (|wptr_| + |rounded_size\), e.g., Chunk 4 in the example above. Note that
  // such a chunk might not exist and we might either reach the end of the
  // buffer or a zeroed region of the buffer.
  // If such a record is found, write a padding chunk exactly at:
  // (position found) - (end of new chunk == |wptr_| + |rounded_size|).
  DcheckIsAlignedAndWithinBounds(wptr_);
  size_t padding_size = 0;
  for (uint8_t* next_chunk_ptr = wptr_;;) {
    ChunkRecord next_chunk = ReadChunkRecordAt(next_chunk_ptr);

    // We just reached the untouched part of the buffer, it's going to be all
    // zeroes from here to end().
    if (next_chunk.size == 0)
      break;

    // We should never hit this, unless we managed to screw up while writing
    // to the buffer and breaking the ChunkRecord(s) chain.
    if (PERFETTO_UNLIKELY(next_chunk_ptr + next_chunk.size > end())) {
      PERFETTO_DCHECK(false);
      PERFETTO_ELOG("TraceBuffer corruption detected. Clearing buffer");
      ClearContentsAndResetRWCursors();
      return;
    }

    // Remove |next_chunk| from the index, as we are about to overwrite it.
    ChunkMeta::Key key(next_chunk);
    const size_t removed = index_.erase(key);
    PERFETTO_DCHECK(next_chunk.is_padding() || removed);

    // |gap_size| is the diff between the end of |next_chunk| and the beginning
    // of the chunk we are about to write @ |wptr_|.
    const size_t gap_size = next_chunk_ptr + next_chunk.size - wptr_;
    if (gap_size >= rounded_size) {
      padding_size = gap_size - rounded_size;
      break;
    }

    next_chunk_ptr += next_chunk.size;
    PERFETTO_DCHECK(next_chunk_ptr >= wptr_ && next_chunk_ptr < end());
  }

  ChunkMeta::Key key(record);
  auto it_and_inserted = index_.emplace(key, ChunkMeta(wptr_));
  PERFETTO_DCHECK(it_and_inserted.second);
  WriteChunkRecord(record, payload, payload_size);
  last_chunk_id_[std::make_pair(producer_id, writer_id)] = chunk_id;

  if (padding_size)
    AddPaddingRecord(padding_size);

  // TODO: add test for the case of: wptr @ Chunk4, New chunk size >>
  // remainder (to force wrapping) but identical to Chunk 1.
}

void TraceBuffez::AddPaddingRecord(size_t size) {
  PERFETTO_DCHECK(size <= kMaxChunkRecordSize);
  ChunkRecord record{};
  record.size = static_cast<decltype(record.size)>(size);
  record.writer_id = ChunkRecord::kWriterIdPadding;
  WriteChunkRecord(record, nullptr, 0);
}

void TraceBuffez::MaybePatchChunkContents(ProducerID producer_id,
                                          WriterID writer_id,
                                          ChunkID chunk_id,
                                          size_t patch_offset_untrusted,
                                          uint32_t patch_value) {
  ChunkMeta::Key key(producer_id, writer_id, chunk_id);
  auto it = index_.find(key);
  if (it == index_.end()) {
    stats_.failed_patches++;
    return;
  }
  const ChunkMeta& chunk_meta = it->second;
  ChunkRecord chunk_record = ReadChunkRecordAt(chunk_meta.begin);

  // Check that the index is consistent with the actual contents of the buffer.
  PERFETTO_DCHECK(ChunkMeta::Key(chunk_record) == key);
  PERFETTO_DCHECK(chunk_meta.begin >= begin());
  uint8_t* chunk_end = chunk_meta.begin + chunk_record.size;
  PERFETTO_DCHECK(chunk_end <= end());

  constexpr size_t kPatchLen = SharedMemoryABI::kPacketHeaderSize;
  static_assert(kPatchLen == sizeof(patch_value),
                "patch_value out of sync with SharedMemoryABI");

  uint8_t* off = chunk_meta.begin + patch_offset_untrusted;
  if (off < chunk_meta.begin || off >= chunk_end - kPatchLen) {
    // Either the IPC was so slow and in the meantime the writer managed to wrap
    // over |chunk_id| or the producer sent a malicious IPC.
    stats_.failed_patches++;
    return;
  }

  // DCHECK that we are writing into a size-field reserved and zero-filled by
  // trace_writer_impl.cc and that we are not writing over other data.
  char zero[kPatchLen]{};
  PERFETTO_DCHECK(memcmp(off, &zero, kPatchLen) == 0);

  memcpy(off, &patch_value, kPatchLen);
  stats_.succeeded_patches++;
}

bool TraceBuffez::ReadNextTracePacket(ChunkSequence* fragments) {
  // constexpr uint8_t kFirstPacketContinuesFromPrevChunk =
  //     SharedMemoryABI::ChunkHeader::kFirstPacketContinuesFromPrevChunk;
  // constexpr uint8_t kLastPacketContinuesOnNextChunk =
  //     SharedMemoryABI::ChunkHeader::kLastPacketContinuesOnNextChunk;

  // The index stores ChunkMeta(s) sorted by {ProducerID, WriterID, ChunkID}.
  // Hence all the ChunkID(s) for the same {Producers, WriterID} are contiguous.
  // Keep in mind however that the sorting by ChunkID is strictly numeric and
  // doesn't keep into account the fact that ChunkID(s) easily wrap over.
  // When iterating over the index, we first identify the subsequence of
  // ChunkMeta(s) that belong to the same {ProducerID, WriterID} and then we
  // iterate over that keeping into account the rounding, using the ChunkID
  // cached into |last_chunk_id_|.
  for (; read_iter_ != index_.end(); read_iter_++) {
    const ChunkMeta::Key& key = read_iter_->first;
    // ChunkMeta& chunk_meta = read_iter_->second;
    auto next_writer_it = index_.upper_bound(
        ChunkMeta::Key(key.producer_id, key.writer_id, kMaxChunkID));

    auto producer_writer_id = std::make_pair(key.producer_id, key.writer_id);
    PERFETTO_DCHECK(last_chunk_id_.count(producer_writer_id));
    const ChunkID last_chunk_id = last_chunk_id_[producer_writer_id];
    const auto& begin_it = read_iter_;
    const auto& end_it = next_writer_it;

    // At this point [begin_it, end_it[ contains ChunkMeta(s) belonging to the
    // same {ProducerID, WriterID}, ordered artithmeticaly by ChunkID.
    // Now we want to find the iterator of the |last_chunk_id| and iterate over
    // the sequence in FIFO order, that is:
    // [last_chunk_it, end_it[, [begin_it, last_chunk_it[
    // Note that until we have wrapped at least once, |last_chunk_it| will be
    // == |end_it|.
    auto last_chunk_it = index_.upper_bound(
        ChunkMeta::Key(key.producer_id, key.writer_id, last_chunk_id));

    for (read_iter_ = last_chunk_it; read_iter_ != end_it; read_iter_++) {
      // if (ReadOnePacket(fragments))
      // return true;
    }

    for (read_iter_ = begin_it; read_iter_ != last_chunk_it; read_iter_++) {
      // if (ReadOnePacket(fragments))
      // return true;
    }

    read_iter_ = next_writer_it;
  }
  return false;
}

// bool TraceBuffez::ReadOnePacket(ChunkSequence* fragments) {
//   if (chunk_meta.read_state == ChunkMeta::kReadAllPackets)
//     return false;
//   uint8_t* chunk_ptr = chunk_meta.begin;
//   ChunkRecord chunk_record = ReadChunkRecordAt(chunk_ptr);
//   PERFETTO_DCHECK(ChunkMeta::Key(chunk_record) == it->first);
//   PERFETTO_DCHECK(chunk_ptr >= begin() && chunk_ptr < end());
//   return false;
//   //
//   // if (read_iter_packet_  >= chunk_record.num_packets)
//   //   return false;
//
//   // chunk_record.flags & kFirstPacketContinuesFromPrevChunk;
// }

}  // namespace perfetto
