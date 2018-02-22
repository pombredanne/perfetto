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
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/tracing/core/shared_memory_abi.h"

// TODO(primiano): Sanitize packets using PacketStreamValidator.

namespace perfetto {

namespace {
// TODO check this constant. I think we went for 4k max.
constexpr size_t kMaxChunkRecordSize =
    0xffff - sizeof(SharedMemoryABI::PageHeader);
constexpr uint8_t kFirstPacketContinuesFromPrevChunk =
    SharedMemoryABI::ChunkHeader::kFirstPacketContinuesFromPrevChunk;
constexpr uint8_t kLastPacketContinuesOnNextChunk =
    SharedMemoryABI::ChunkHeader::kLastPacketContinuesOnNextChunk;

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
  read_iter_ = GetReadIterForSequence(index_.end());
}

// Note: |src| points to a shmem region that is shared with the producer. Assume
// that the producer is malicious and will change the content of |src|
// while we execute here. Don't do any processing on it other than memcpy().
void TraceBuffez::CopyChunkUntrusted(ProducerID producer_id,
                                     WriterID writer_id,
                                     ChunkID chunk_id,
                                     uint8_t num_packets,
                                     uint8_t flags,
                                     const uint8_t* src,
                                     size_t size) {
  PERFETTO_DCHECK(writer_id != ChunkRecord::kWriterIdPadding);

  // TODO(primiano): return if size > 4k.

  // Avoids that we end up in a fragmented state where available_size()
  // is > 0 but < sizeof(ChunkRecord).
  size_t rounded_size =
      base::Align<sizeof(ChunkRecord)>(size + sizeof(ChunkRecord));

  ChunkRecord record{};
  static_assert(kMaxChunkRecordSize <=
                    std::numeric_limits<decltype(record.size())>::max(),
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
  // TODO(primiano): add a test for the case of exactly sizeof(ChunkRecord)
  // bytes left at the end of the buffer.

  record.producer_id = producer_id;
  record.chunk_id = chunk_id;
  record.writer_id = writer_id;
  record.flags = flags;
  record.set_size(rounded_size);

  // At this point either |wptr_| points to an untouched part of the buffer
  // (i.e. *wptr_ == 0) or we are about to overwrite one or more ChunkRecord(s).
  // In the latter case we need to first figure out where the next valid
  // ChunkRecord is (if it exists) and add padding between the new record.
  // Example ((w) == write cursor):
  //
  // Initial state (wtpr_ == 0):
  // |0 (w)    |10               |30                  |50
  // +---------+-----------------+--------------------+--------------------+
  // | Chunk 1 | Chunk 2         | Chunk 3            | Chunk 4            |
  // +---------+-----------------+--------------------+--------------------+
  //
  // Let's assume we now want now write a 5th Chunk of size == 35. The final
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
  // (position found) - (end of new chunk, that is |wptr_| + |rounded_size|).
  DcheckIsAlignedAndWithinBounds(wptr_);
  size_t padding_size = 0;

  // This loop removes from the index all the chunks that are going to be
  // overwritten (chunks 1-3 in the example above) and computes the padding.
  for (uint8_t* next_chunk_ptr = wptr_;;) {
    const ChunkRecord& next_chunk = *GetChunkRecordAt(next_chunk_ptr);

    // We just reached the untouched part of the buffer, it's going to be all
    // zeroes from here to end().
    if (!next_chunk.is_valid())
      break;

    // We should never hit this, unless we managed to screw up while writing
    // to the buffer and breaking the ChunkRecord(s) chain.
    if (PERFETTO_UNLIKELY(next_chunk_ptr + next_chunk.size() > end())) {
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
    const size_t gap_size = next_chunk_ptr + next_chunk.size() - wptr_;
    if (gap_size >= rounded_size) {
      padding_size = gap_size - rounded_size;
      break;
    }

    next_chunk_ptr += next_chunk.size();
    PERFETTO_DCHECK(next_chunk_ptr >= wptr_ && next_chunk_ptr < end());
  }

  ChunkMeta::Key key(record);
  auto it_and_inserted = index_.emplace(
      key, ChunkMeta(GetChunkRecordAt(wptr_), flags, num_packets));
  PERFETTO_DCHECK(it_and_inserted.second);
  WriteChunkRecord(record, src, size);
  last_chunk_id_[std::make_pair(producer_id, writer_id)] = chunk_id;

  if (padding_size)
    AddPaddingRecord(padding_size);

  // TODO: add test for the case of: wptr @ Chunk4, New chunk size >>
  // remainder (to force wrapping) but identical to Chunk 1.
}

void TraceBuffez::AddPaddingRecord(size_t size) {
  PERFETTO_DCHECK(size <= kMaxChunkRecordSize);
  ChunkRecord record{};
  record.set_size(size);
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

  // Check that the index is consistent with the actual contents of the buffer.
  PERFETTO_DCHECK(ChunkMeta::Key(*chunk_meta.chunk_record) == key);
  uint8_t* chunk_begin = reinterpret_cast<uint8_t*>(chunk_meta.chunk_record);
  PERFETTO_DCHECK(chunk_begin >= begin());
  uint8_t* chunk_end = chunk_begin + chunk_meta.chunk_record->size();
  PERFETTO_DCHECK(chunk_end <= end());

  constexpr size_t kPatchLen = SharedMemoryABI::kPacketHeaderSize;
  static_assert(kPatchLen == sizeof(patch_value),
                "patch_value out of sync with SharedMemoryABI");

  uint8_t* off = chunk_begin + patch_offset_untrusted;
  if (off < chunk_begin || off >= chunk_end - kPatchLen) {
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

void TraceBuffez::BeginRead() {
  read_iter_ = GetReadIterForSequence(index_.begin());
}

// The index_ looks like this:
// ProducerID | WriterID  | ChunkID | ChunkMeta
// -----------+-----------+---------+----------
//     1           1          1
//     1           1          2
//     1           1          3
//     1           2          1
//     1           2          2
//     1           2          3
//     2           1          1
//     2           1          2
TraceBuffez::IndexIterator TraceBuffez::GetReadIterForSequence(
    IndexMap::iterator begin) {
// Note: |begin| might be == index_.end() if |index_| is empty.

#if PERFETTO_DCHECK_IS_ON()
  // Either |begin| is == index_.begin() or the item immediately before must
  // belong to a different {ProducerID, WriterID} sequence.
  auto prev_it = begin;
  prev_it--;
  PERFETTO_DCHECK(
      begin == index_.begin() ||
      std::tie(prev_it->first.producer_id, prev_it->first.writer_id) <
          std::tie(begin->first.producer_id, begin->first.writer_id));
#endif
  IndexIterator iter;
  iter.begin = begin;

  // Find the first entry that has a greater {ProducerID, WriterID} (or just
  // index_.end() if we reached the end).
  ChunkMeta::Key key = begin->first;  // Deliberate copy.
  key.chunk_id = kMaxChunkID;
  iter.end = index_.upper_bound(key);

  if (PERFETTO_UNLIKELY(iter.begin == iter.end)) {
    iter.cur = iter.end;
    return iter;
  }

  // Now find the first entry between [begin, end[ that is > ChunkID. This is
  // where we the sequence will start (see notes about wrapping in the header).
  auto producer_and_writer_id = std::make_pair(key.producer_id, key.writer_id);
  PERFETTO_DCHECK(last_chunk_id_.count(producer_and_writer_id));
  iter.wrapping_id = last_chunk_id_[producer_and_writer_id];
  key.chunk_id = iter.wrapping_id;
  iter.cur = index_.upper_bound(key);
  PERFETTO_DCHECK(iter.cur != iter.end);
  if (iter.cur == iter.end)
    iter.cur = iter.begin;
  return iter;
  // TODO: add separate test coverage for the iterator.
}

void TraceBuffez::IndexIterator::MoveNext() {
  // Note: |begin| might be == |end|.
  if (cur == end || cur->first.chunk_id == wrapping_id) {
    cur = end;
    return;
  }
  if (++cur == end)
    cur = begin;
}

bool TraceBuffez::ReadNextTracePacket(Slices* slices) {
  // Note: MoveNext() moves only within the next chunk within the same
  // {ProducerID, WriterID} sequence. Here we want to:
  // - return the next packet in the current sequence, if any.
  // - return the first packed in the next sequence, if any.
  // - return false if none of the above is found.
  for (;; read_iter_.MoveNext()) {
    if (PERFETTO_UNLIKELY(!read_iter_.is_valid())) {
      // We ran out of chunks in the current {ProducerID, WriterID} sequence or
      // we just reached the index_.end().
      if (PERFETTO_UNLIKELY(read_iter_.end == index_.end()))
        return false;

      // Move to the next sequence.
      read_iter_ = GetReadIterForSequence(++read_iter_.end);
      PERFETTO_DCHECK(read_iter_.is_valid());
    }

    ChunkMeta& chunk_meta = *read_iter_;

    // At this point we have a chunk in |chunk_meta| that has not been fully
    // read. We don't know yet whether we have enough data to read the full
    // packet (in the case it's fragmented over seeral chunks) and we are about
    // to find that out. Specifically:
    // A) If the first packet is unread and is a fragment continuing from a
    //    previous chunk, it means we have missed the previous ChunkID. In
    //    fact, if this wasn't the case, a previous call to ReadNext() shouldn't
    //    have moved the cursor to this chunk.
    // B) Any packet > 0 && < last is always eligible to be read. By definition
    //    an inner packet is never fragmented and hence doesn't require neither
    //    stitching nor any out-of-band patching. The same applies to the last
    //    packet iff it doesn't continue on the next chunk.
    // C) If the last packet (which might be also the only packet in the chunk)
    //    is a fragment and continues on the next chunk, we peek at the next
    //    chunks and, if we have all of them, mark as read and move the cursor.
    //
    // +---------------+   +-------------------+  +---------------+
    // | ChunkID: 1    |   | ChunkID: 2        |  | ChunkID: 3    |
    // |---------------+   +-------------------+  +---------------+
    // | Packet 1      |   |                   |  | ... Packet 3  |
    // | Packet 2      |   | ... Packet 3  ... |  | Packet 4      |
    // | Packet 3  ... |   |                   |  | Packet 5 ...  |
    // +---------------+   +-------------------+  +---------------+

    PERFETTO_DCHECK(chunk_meta.num_packets_read <= chunk_meta.num_packets);
    while (chunk_meta.num_packets_read < chunk_meta.num_packets) {
      enum { kSkip = 0, kReadOnePacket, kTryReadAhead } action;
      if (chunk_meta.num_packets_read == 0) {
        if (chunk_meta.flags & kFirstPacketContinuesFromPrevChunk) {
          action = kSkip;  // Case A.
        } else if (chunk_meta.num_packets == 1 &&
                   (chunk_meta.flags & kLastPacketContinuesOnNextChunk)) {
          action = kTryReadAhead;  // Case C.
        } else {
          action = kReadOnePacket;  // Case B.
        }
      } else if (chunk_meta.num_packets_read < chunk_meta.num_packets - 1 ||
                 !(chunk_meta.flags & kLastPacketContinuesOnNextChunk)) {
        action = kReadOnePacket;  // Case B.
      } else {
        action = kTryReadAhead;  // Case C.
      }

      if (action == kSkip) {
        ReadNextPacket(&chunk_meta);
        continue;
      }

      if (action == kReadOnePacket) {
        slices->push_back(ReadNextPacket(&chunk_meta));
        return true;
      }

      PERFETTO_DCHECK(action == kTryReadAhead);

      // This is the complex case. At this point we have to look ahead in the
      // |index_| to see if we have all the chunks required to complete this
      // packet. If we do, we want to mark all those chunks as read and put
      // them into |slices|. If we don't we do NOT want to chang their read
      // state but we want to skip to the next {ProducerID, WriterID}
      // sequence.

      // Here we are going to speculatively fill |slices| until we either
      // find out that we had all packets, or we should just skip and move
      // to the next {ProducerID,WriterID} sequence.
      slices->push_back(ReadNextPacket(&chunk_meta));

      // TODO(primiano): optimization: here we are copying over the full
      // iterator object, but the only thing we change is the |cur| and
      // all the other fields stay the same. Do something better.
      static_assert(static_cast<ChunkID>(kMaxChunkID + 1) == 0,
                    "relying on kMaxChunkID to wrap naturally");
      ChunkID next_chunk_id = read_iter_.chunk_id() + 1;
      for (IndexIterator it = read_iter_; it.is_valid();
           it.MoveNext(), next_chunk_id++) {
        PERFETTO_DCHECK(it.producer_id() == read_iter_.producer_id() &&
                        it.writer_id() == read_iter_.writer_id());

        if (it.chunk_id() != next_chunk_id || (*it).num_packets == 0) {
          // The sequence is broken, we missed a ChunkID.
          // Now we have to unwind the |num_packets_read| that have been
          // incremented by mis-speculating on the success of having all chunks.

          // TODO update |stats_|.

          for (; read_iter_.cur != it.cur; read_iter_.MoveNext()) {
            PERFETTO_DCHECK(read_iter_.is_valid());
            if (PERFETTO_UNLIKELY((*read_iter_).num_packets_read == 0))
              continue;
            (*read_iter_).num_packets_read--;
            // TODO: add a test for this.
          }
          slices->clear();
          read_iter_.cur = read_iter_.end;
          break;  // goes back to the for(;;MoveNext()) (see other break below).
        }

        slices->push_back(ReadNextPacket(&chunk_meta));

        if (!((*it).flags & kLastPacketContinuesOnNextChunk))
          return true;  // We made it.

        // The packet continues on the next chunk. We have still hope.
        // Keep looping in the next chunks of the same sequence.
      }
      break;
    }  // while(packets)
  }    // for(;;MoveNext())
}

Slice TraceBuffez::ReadNextPacket(ChunkMeta* chunk_meta) {
  PERFETTO_DCHECK(chunk_meta->num_packets_read < chunk_meta->num_packets);
  // TODO DCHECK for unpatched.

  const uint8_t* record_begin =
      reinterpret_cast<const uint8_t*>(chunk_meta->chunk_record);
  const uint8_t* packets_begin = record_begin + sizeof(ChunkRecord);
  const uint8_t* record_end = record_begin + chunk_meta->chunk_record->size();
  const uint8_t* packet_begin = packets_begin + chunk_meta->cur_packet_offset;
  if (packet_begin < begin() || packet_begin >= end()) {
    PERFETTO_DCHECK(false);
    // TODO What to do with num_packets_read?
    return Slice();  // TODO(primiano): add test coverage.
  }

  // A packet (or a fragment) starts with a varint stating its size, followed
  // by its content.
  uint64_t packet_size_64 = 0;
  using protozero::proto_utils::ParseVarInt;
  const uint8_t* packet_data =
      ParseVarInt(packet_begin, record_end, &packet_size_64);
  const size_t packet_size = static_cast<size_t>(packet_size_64);
  const uint8_t* next_packet = packet_data + packet_size;
  if (next_packet <= packet_begin || next_packet > record_end) {
    PERFETTO_DCHECK(false);
    // TODO What to do with num_packets_read?
    return Slice();  // TODO(primiano): add test coverage.
  }
  PERFETTO_CHECK(next_packet - packets_begin <
                 static_cast<ptrdiff_t>(chunk_meta->chunk_record->size()));
  chunk_meta->cur_packet_offset =
      static_cast<uint16_t>(next_packet - packets_begin);
  chunk_meta->num_packets_read++;
  return Slice(packet_data, packet_size);
  // TODO recheck all this.
}

}  // namespace perfetto
