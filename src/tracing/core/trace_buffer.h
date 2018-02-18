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

namespace perfetto {

// TODO description.
// explain the difference between ChunkRecord and ChunkMeta.
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
  void CopyChunkFromUntrustedShmem(ProducerID producer_id,
                                   WriterID writer_id,
                                   uint16_t chunk_id,
                                   uint8_t flags,
                                   const uint8_t* payload,
                                   size_t payload_size);

  // Patches SharedMemoryABI::kPacketHeaderSize bytes at offset |patch_offset|
  // replacing them with the contents of patch_value if the given chunk exists.
  void MaybePatchChunkContents(ProducerID,
                               WriterID,
                               ChunkID,
                               size_t patch_offset,
                               uint32_t patch_value);

  const Stats& stats() const { return stats_; }

 private:
  // ChunkRecord is a Chunk header stored inlined in the |data_| buffer, before
  // each copied chunk's payload. The |data_| buffer looks like this:
  // +---------------+-------------------++---------------+----------------+
  // | ChunkRecord 1 | Chunk payload 1   || ChunkRecord 2 | Chunk payload 2| ...
  // +---------------+-------------------++---------------+----------------+
  // It contains all the data necessary to reconstruct the contents of the
  // |data_| buffer from a coredump in the case of a crash (i.e. without using
  // any data in the lookaside |index_|).
  // Most of the ChunkRecord fields are copied from SharedMemoryABI::ChunkHeader
  // (the chunk header used in the the shared memory buffers).
  // Furthermore a ChunkRecord can be a special "padding" record. In this case
  // the payload should be ignored and the record just skipped.
  // Full page move optimizations:
  //   This struct has to be exactly (sizeof(PageHeader) + sizeof(ChunkHeader))
  //   (from shared_memory_abi.h) to allow full page move optimizations (TODO
  //   not yet implemented). In the special case of moving a full 4k page that
  //   contains only one chunk, in fact, we can just move the full SHM page and
  //   overlay the ChunkRecord on top of the moved SMB's (page + chunk) header.
  //   This is checked via static_assert(s) in the .cc file.
  struct ChunkRecord {
    static constexpr WriterID kWriterIdPadding = 0;

    bool is_padding() const { return writer_id == kWriterIdPadding; }

    uint16_t size;  // Size in bytes, incl. the size of the ChunkRecord itself.

    // Unique per Producer (but not within the service).
    // If writer_id == kWriterIdPadding the record should just be skipped.
    WriterID writer_id;

    ChunkID chunk_id;  // Monotonic counter within the same writer id.
    uint8_t flags;     // see SharedMemoryABI::ChunkHeader::flags.
    uint8_t padding_unused;

    ProducerID producer_id;
  };

  // Lookaside structure stored outside of the |data_| buffer (hence not easily
  // accessible at crash dump time). This structure serves two purposes:
  // 1) Allow a fast lookup of ChunkRecord by their address (the tuple
  //   {ProducerID, WriterID, ChunkID}). This is used when applying out-of-band
  //   patches to the contents of the chunks.
  // 2) Keep metadata about the status of the chunk, e.g. whether the contents
  //   have been read already and should be skipped in a future read pass.
  // This struct should not have any field that is essential for reconstructing
  // the contents of the buffer from a coredump (use ChunkRecord in that case).
  struct ChunkMeta {
    struct Key {
      Key(ProducerID p, WriterID w, ChunkID c)
          : producer_id{p}, writer_id{w}, chunk_id{c} {}

      explicit Key(const ChunkRecord& cr)
          : Key(cr.producer_id, cr.writer_id, cr.chunk_id) {}

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
      // purposes as they are the key used for the lookup in the set.
      ProducerID producer_id;
      WriterID writer_id;
      ChunkID chunk_id;
    };

    ChunkMeta(uint8_t* addr) : begin{addr} {}

    // Address of the corresponding ChunkRecord in |data_|.
    uint8_t* begin;
  };

  TraceBuffez(const TraceBuffez&) = delete;
  TraceBuffez& operator=(const TraceBuffez&) = delete;

  void ClearContentsAndResetRWCursors();

  void AddPaddingRecord(size_t);

  void DcheckIsAlignedAndWithinBounds(const uint8_t* off) const {
    PERFETTO_DCHECK(off >= begin() && off <= end() - sizeof(ChunkRecord));
    PERFETTO_DCHECK(
        (reinterpret_cast<uintptr_t>(off) & (alignof(ChunkRecord) - 1)) == 0);
  }

  // This function simply casts a pointer within |data_| to an aligned pointer
  // to ChunkRecord. This pointer should NOT be used directly but passed to
  // memcpy(). The cast hints to the compiler to assume that the pointer is
  // aligned, which in turn short-circuits the memcpy() that the caller into a
  // simpler mov.
  // const ChunkRecord* GetAlignedPtrForMemcpy(const uint8_t* off) const {
  //   DcheckIsAlignedAndWithinBounds(off);
  //   return reinterpret_cast<const ChunkRecord*>(off);
  // }

  // size_t ReadChunkRecordSizeAt(const uint8_t* off) const {
  //   const ChunkRecord* aligned_ptr = GetAlignedPtrForMemcpy(off);
  //   decltype(aligned_ptr->size) size;
  //   memcpy(&size, &aligned_ptr->size, sizeof(size));
  //
  //   // We should saturate the size read from the record, to prevent that a
  //   // corrupted record causes overflows. Right now |size| is a uint16_t and
  //   // the saturation is implicit. However, if that should change this
  //   function
  //   // (and also ReadChunkRecordAt below) will require an explicit
  //   saturation. static_assert(sizeof(size) < sizeof(size_t), "Add saturation
  //   logic");
  //
  //   return size;
  // }

  ChunkRecord ReadChunkRecordAt(const uint8_t* off) const {
    DcheckIsAlignedAndWithinBounds(off);
    ChunkRecord res;

    // The reinterpret_cast hints the compiler about the fact that |off| is
    // aligned and hence allows it to perform an optimized memcpy().
    memcpy(&res, reinterpret_cast<const ChunkRecord*>(off), sizeof(res));
    return res;
  }

  // |payload| can be nullptr (in which case payload_size must be == 0), for the
  // case of writing a pading record. In this case the |wptr_| is advanced
  // regularly (accordingly to |record.size|) but no payload is copied.
  // TODO: this seems used in one place only.
  void WriteChunkRecord(const ChunkRecord& record,
                        const uint8_t* payload,
                        size_t payload_size) {
    DcheckIsAlignedAndWithinBounds(wptr_);
    // Note: |record.size| might be slightly bigger than |payload_size| because
    // of rounding, to ensure that all ChunkRecord(s) are multiple of
    // sizeof(ChunkRecord). The invariant is:
    // record.size >= |payload_size| + sizeof(ChunkRecord) (== if no rounding).
    PERFETTO_DCHECK(record.size <= writable_size());
    PERFETTO_DCHECK(record.size >= sizeof(ChunkRecord));
    PERFETTO_DCHECK((record.size & (sizeof(ChunkRecord) - 1)) == 0);
    PERFETTO_DCHECK(record.size >= payload_size + sizeof(ChunkRecord));

#if !defined(NDEBUG)
    memset(wptr_, 0xFF, record.size);
#endif
    memcpy(wptr_, &record, sizeof(record));
    if (PERFETTO_LIKELY(payload))
      memcpy(wptr_ + sizeof(record), payload, payload_size);
    wptr_ += record.size;

    // TODO advance rptr as well in case of wrapping.
    if (wptr_ >= end() - sizeof(ChunkRecord))
      wptr_ = begin();

    DcheckIsAlignedAndWithinBounds(wptr_);
  }

  base::PageAllocator::UniquePtr data_;
  size_t size_ = 0;

  uint8_t* begin() const { return reinterpret_cast<uint8_t*>(data_.get()); }
  uint8_t* end() const { return begin() + size_; }
  size_t writable_size() const { return end() - wptr_; }

  uint8_t* wptr_ = nullptr;
  uint8_t* rptr_ = nullptr;

  // An index that keep track of the positions and metadata of each ChunkRecord.
  std::map<ChunkMeta::Key, ChunkMeta> index_;

  Stats stats_ = {};
};

}  // namespace perfetto

#endif  // SRC_TRACING_CORE_TRACE_BUFFER_H_
