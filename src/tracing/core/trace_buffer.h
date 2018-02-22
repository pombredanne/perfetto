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

#ifndef SRC_TRACING_CORE_TRACE_BUFFER_H_
#define SRC_TRACING_CORE_TRACE_BUFFER_H_

#include <stdint.h>
#include <string.h>

#include <map>
#include <tuple>

#include "perfetto/base/logging.h"
#include "perfetto/base/page_allocator.h"
#include "perfetto/tracing/core/basic_types.h"
#include "perfetto/tracing/core/slice.h"

namespace perfetto {

// TODO(primiano): we need some flag to figure out if the packets on the
// boundary require patching or have alrady been patched. The current
// implementation considers all packets eligible to be read once we have all the
// chunks that compose them.

// - The buffer contains data written by several Producer(s), identified by
// their
//   ProducerID.
// - Each producer writes several sequences identified by the same WriterID.
// - Each Writer writes, in order, several chunks.
// - Each Chunk contains a fragment, one, or more TracePacket(s)

// TODO description.
class TraceBuffez {
 public:
  struct Stats {
    size_t failed_patches = 0;
    size_t succeeded_patches = 0;
  };

  TraceBuffez();
  TraceBuffez(TraceBuffez&&) noexcept;
  TraceBuffez& operator=(TraceBuffez&&);

  // TODO make this a static unique_ptr factory.
  bool Create(size_t size);

  // Copies a Chunk from a producer Shared Memory Buffer into the trace buffer.
  void CopyChunkUntrusted(ProducerID producer_id,
                          WriterID writer_id,
                          ChunkID chunk_id,
                          uint8_t num_packets,
                          uint8_t flags,
                          const uint8_t* payload,
                          size_t payload_size);

  // Patches N=SharedMemoryABI::kPacketHeaderSize bytes at offset |patch_offset|
  // replacing them with the contents of patch_value, if the given chunk exists.
  // Does nothig if the given ChunkID is gone.
  void MaybePatchChunkContents(ProducerID,
                               WriterID,
                               ChunkID,
                               size_t patch_offset,
                               uint32_t patch_value);

  // To read the contents the caller needs to:
  //   BeginRead()
  //   while (ReadNext(packet_fragments)) { ... }
  // No other calls should be interleaved between BeginRead() and ReadNext().
  // Reads in the TraceBuffer are NOT idempotent.

  void BeginRead();

  // Returns the next packet in the buffer if any. This function returns only
  // complete packets. Specifically:
  // When there is at least one whole packet in the buffer, this function
  // returns true and populates the |fragments| argument with the boundaries of
  // each fragment for one packet (|fragments|.size() will be >= 1).
  // When there are no whole packets eligible to read (e.g. we are still missing
  // fragments) this function returns false and clears |fragments|.
  // This function guarantees also that packets for a given
  // {ProducerID, WriterID} are read in FIFO order.
  // This function does not guarantee any ordering w.r.t. packets belonging to
  // different WriterID(s). For instance, given the following packets copied
  // into the buffer:
  //   {ProducerID: 1, WriterID: 1}: P1 P2 P3
  //   {ProducerID: 1, WriterID: 2}: P4 P5 P6
  //   {ProducerID: 2, WriterID: 1}: P7 P8 P9
  // The following read sequence is possible:
  //   P1, P4, P7, P2, P3, P5, P8, P9, P6
  // But the following is guaranteed to NOT happen:
  //   P1, P5, P7, P4 (P4 cannot come after P5)
  bool ReadNextTracePacket(Slices* slices);

  const Stats& stats() const { return stats_; }

 private:
  // ChunkRecord is a Chunk header stored inline in the |data_| buffer, before
  // each copied chunk's payload. The |data_| buffer looks like this:
  // +---------------+------------------++---------------+-----------------+
  // | ChunkRecord 1 | Chunk payload 1  || ChunkRecord 2 | Chunk payload 2 | ...
  // +---------------+------------------++---------------+-----------------+
  // It contains all the data necessary to reconstruct the contents of the
  // |data_| buffer from a coredump in the case of a crash (i.e. without using
  // any data in the lookaside |index_|).
  // Most of the ChunkRecord fields are copied from SharedMemoryABI::ChunkHeader
  // (the chunk header used in the the shared memory buffers).
  // A ChunkRecord can be a special "padding" record. In this case its payload
  // should be ignored and the record should be just skipped.
  //
  // Full page move optimization:
  // This struct has to be exactly (sizeof(PageHeader) + sizeof(ChunkHeader))
  // (from shared_memory_abi.h) to allow full page move optimizations (TODO
  // not yet implemented). In the special case of moving a full 4k page that
  // contains only one chunk, in fact, we can just move the full SHM page and
  // overlay the ChunkRecord on top of the moved SMB's (page + chunk) header.
  // This requirement is covered by static_assert(s) in the .cc file.
  struct ChunkRecord {
    static constexpr WriterID kWriterIdPadding = 0;

    bool is_padding() const { return writer_id == kWriterIdPadding; }
    size_t size() const { return size_div_16 * 16UL + 16; }
    bool is_valid() const { return size_div_16 != 0; }
    void set_size(size_t size) {
      PERFETTO_DCHECK(size > 0 && size % 16 == 0);
      size_div_16 = static_cast<uint8_t>((size - 16) / 16);
    }

    // [64 bits] ID of the Producer from which the Chunk was copied from.
    ProducerID producer_id;

    // [32 bits] Monotonic counter within the same writer_id.
    ChunkID chunk_id;

    // [16 bits] Unique per Producer (but not within the service).
    // If writer_id == kWriterIdPadding the record should just be skipped.
    WriterID writer_id;

    uint8_t flags;  // See SharedMemoryABI::ChunkHeader::flags.

    // Size in 16B blocks, starts @ 16 (0 == 16, 1 == 32... 0xff == 4096).
    // The size of a chunk includes the ChunkRecord itself.
    uint8_t size_div_16;
  };

  // Lookaside structure stored outside of the |data_| buffer (hence not
  // accessible from a core dump). This structure serves two purposes:
  // 1) Allow a fast lookup of ChunkRecord by their ID (the tuple
  //   {ProducerID, WriterID, ChunkID}). This is used when applying out-of-band
  //   patches to the contents of the chunks after they have been copied into
  //   the TraceBuffer.
  // 2) Keep metadata about the status of the chunk, e.g. whether the contents
  //   have been read already and should be skipped in a future read pass.
  // This struct should not have any field that is essential for reconstructing
  // the contents of the buffer from a coredump.
  struct ChunkMeta {
    struct Key {
      Key(ProducerID p, WriterID w, ChunkID c)
          : producer_id{p}, writer_id{w}, chunk_id{c} {}

      explicit Key(const ChunkRecord& cr)
          : Key(cr.producer_id, cr.writer_id, cr.chunk_id) {}

      // Note that this sorting doesn't keep into account the fact that ChunkID
      // will wrap over at some point. The extra logic in ChunkIterator deals
      // with that.
      bool operator<(const Key& other) const {
        return std::tie(producer_id, writer_id, chunk_id) <
               std::tie(other.producer_id, other.writer_id, other.chunk_id);
      }

      bool operator==(const Key& other) const {
        return std::tie(producer_id, writer_id, chunk_id) ==
               std::tie(other.producer_id, other.writer_id, other.chunk_id);
      }

      // These fields should match at all times the corresponding fields in
      // the ChunkRecord @ |begin|. They are copied here purely for efficiency
      // to avoid dereferencing the buffer all the times.
      ProducerID producer_id;
      WriterID writer_id;
      ChunkID chunk_id;
    };

    ChunkMeta(ChunkRecord* c, uint16_t p, uint8_t f)
        : chunk_record{c},
          flags{f},
          num_packets{p},
          cur_packet_offset{sizeof(ChunkRecord)} {}

    ChunkRecord* const chunk_record;  // Addr of ChunkRecord within |data_|.
    const uint8_t flags = 0;
    const uint16_t num_packets = 0;  // Total number of packets.
    uint16_t num_packets_read = 0;   // Number of packets already read.

    // The start offset offset of the next packet to be read (that is the
    // |num_packets_read|-th) computed from the beginning of the ChunkRecord's
    // payload (the 1st packet starts at |chunk_record| + sizeof(ChunkRecord)).
    uint16_t cur_packet_offset = 0;

    // If != 0 the last packet in the chunk cannot be read, even if the
    // subsequent ChunkID is already available, until a patch at offset
    // |last_packet_patch_offset| is applied through MaybePatchChunkContents().
    // TODO(primiano): use this, currently unused.
    uint16_t last_packet_patch_offset = 0;
  };

  using ChunkMap = std::map<ChunkMeta::Key, ChunkMeta>;

  // Allows to iterate over a sub-sequence of |index_| for all keys belonging to
  // the same {ProducerID,WriterID}. Furthermore takes into the wrapping of
  // ChunkID. Iterator instances are valid only as long as the |index_| is not
  // altered (i.e. can be used safely only between ReadNextPacket() calls).
  // The order of the iteration will proceed in the following order:
  // wrapping_id + 1 -> end, begin -> wrapping_id.
  // Practical example:
  // - Assume that kMaxChunkID == 7
  // - Assume that we have all 8 chunks in the range (0..7).
  // - Hence, begin == c0, end == c7
  // - Assume wrapping_id = 4 (c4 is the last chunk copied over
  //   through a CopyChunkUntrusted()).
  // The resulting iteration order will be: c5, c6, c7, c0, c1, c2, c3, c4.
  struct ChunkIterator {
    // Points to the 1st key (the one with the numerically min ChunkID).
    ChunkMap::iterator begin;

    // Points one past the last key (the one with the numerically max ChunkID).
    ChunkMap::iterator end;

    // Current iterator, always >= begin && <= end.
    ChunkMap::iterator cur;

    // The latest ChunkID written. Determines the start/end of the sequence.
    ChunkID wrapping_id;

    bool is_valid() const { return cur != end; }

    ProducerID producer_id() const {
      PERFETTO_DCHECK(is_valid());
      return cur->first.producer_id;
    }

    WriterID writer_id() const {
      PERFETTO_DCHECK(is_valid());
      return cur->first.writer_id;
    }

    ChunkID chunk_id() const {
      PERFETTO_DCHECK(is_valid());
      return cur->first.chunk_id;
    }

    // TODO(primiano): memcpy?
    ChunkMeta& operator*() {
      PERFETTO_DCHECK(is_valid());
      return cur->second;
    }

    // Moves |cur| to the next chunk in the index.
    // is_valid() will become false after calling this, if this was the last
    // entry of the sequence.
    void MoveNext();
  };

  TraceBuffez(const TraceBuffez&) = delete;
  TraceBuffez& operator=(const TraceBuffez&) = delete;

  // Returns an object that allows to iterate over chunks in the |index_| that
  // have the same {ProducerID, WriterID} of begin.first.{producer,writer}_id.
  // |beging| must be an iterator to the first entry in the |index_| that has a
  // different {ProducerID, WriterID} from the previous one (or index_.begin()).
  // It is valid for |begin| to be == index_.end().
  // The iteration takes care of ChunkID wrapping, by using |last_chunk_id_|.
  ChunkIterator GetReadIterForSequence(ChunkMap::iterator begin);

  // Used in the last resort case when a buffer corruption is detected.
  void ClearContentsAndResetRWCursors();

  // Adds a padding record of the given size (must be a multiple of
  // sizeof(ChunkRecord)).
  void AddPaddingRecord(size_t);

  // Returns the boundaries of the next packet (or a fragment) pointed by
  // ChunkMeta. It increments the |num_packets_read| counter.
  Slice ReadNextPacket(ChunkMeta*);

  void DcheckIsAlignedAndWithinBounds(const uint8_t* off) const {
    PERFETTO_DCHECK(off >= begin() && off <= end() - sizeof(ChunkRecord));
    PERFETTO_DCHECK(
        (reinterpret_cast<uintptr_t>(off) & (alignof(ChunkRecord) - 1)) == 0);
  }

  ChunkRecord* GetChunkRecordAt(uint8_t* off) const {
    DcheckIsAlignedAndWithinBounds(off);
    return reinterpret_cast<ChunkRecord*>(off);
  }

  // |src| can be nullptr (in which case |size| must be == 0), for the
  // case of writing a pading record. In this case the |wptr_| is advanced
  // regularly (accordingly to |record.size|) but no payload is copied.
  void WriteChunkRecord(const ChunkRecord& record,
                        const uint8_t* src,
                        size_t size) {
    DcheckIsAlignedAndWithinBounds(wptr_);
    // Note: |record.size| might be slightly bigger than |size| because
    // of rounding, to ensure that all ChunkRecord(s) are multiple of
    // sizeof(ChunkRecord). The invariant is:
    // record.size() >= |size| + sizeof(ChunkRecord) (== if no
    // rounding).
    PERFETTO_DCHECK(record.size() <= writable_size());
    PERFETTO_DCHECK(record.size() >= sizeof(record));
    PERFETTO_DCHECK(record.size() % sizeof(record) == 0);
    PERFETTO_DCHECK(record.size() >= size + sizeof(record));

    memcpy(wptr_, &record, sizeof(record));
    if (PERFETTO_LIKELY(src))
      memcpy(wptr_ + sizeof(record), src, size);
    const size_t rounding_size = record.size() - size - sizeof(record);
    memset(wptr_ + sizeof(record) + size, 0, rounding_size);
    wptr_ += record.size();

    if (wptr_ >= end())
      wptr_ = begin();

    DcheckIsAlignedAndWithinBounds(wptr_);
  }

  uint8_t* begin() const { return reinterpret_cast<uint8_t*>(data_.get()); }
  uint8_t* end() const { return begin() + size_; }

  // Distance in bytes between wptr_ and the end of the buffer.
  size_t writable_size() const { return end() - wptr_; }

  base::PageAllocator::UniquePtr data_;
  size_t size_ = 0;  // size in bytes of |data_|.

  uint8_t* wptr_ = nullptr;  // Write pointer.

  // An index that keep track of the positions and metadata of each
  // ChunkRecord.
  ChunkMap index_;

  // Read iterator used for ReadNext(). It is reset by calling BeginRead().
  // It becomes invalid after any call to methods that alters the |_index|.
  ChunkIterator read_iter_;

  // Keeps track of the last ChunkID written for a given writer.
  // TODO(primiano): we should clean up keys from this map. Right now this map
  // grows without bounds (although realistically is not a problem unless we
  // have too many producers within the same trace session).
  std::map<std::pair<ProducerID, WriterID>, ChunkID> last_chunk_id_;

  // Statistics about buffer usage.
  Stats stats_ = {};
};

}  // namespace perfetto

#endif  // SRC_TRACING_CORE_TRACE_BUFFER_H_
