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

#include "perfetto/base/logging.h"
#include "perfetto/base/page_allocator.h"

namespace perfetto {

// Overlay on the buffer |data_| itself?
class TraceBuffez {
 public:
  TraceBuffez();
  TraceBuffez(TraceBuffez&&) noexcept;
  TraceBuffez& operator=(TraceBuffez&&);

  // TODO make this a static unique_ptr factory.
  bool Create(size_t size);

  // Copies a Chunk from a producer Shared Memory Buffer into the trace buffer.
  void CopyChunkFromUntrustedShmem(uint16_t producer_id,
                                   uint16_t writer_id,
                                   uint16_t chunk_id,
                                   uint8_t flags,
                                   const uint8_t* payload,
                                   size_t payload_size);

 private:
  // This struct has to be exactly (sizeof(PageHeader) + sizeof(ChunkHeader))
  // (from shared_memory_abi.h) to allow full page moving optimizations.
  // In the special case of moving a full 4K page that contains only one chunk,
  // in fact, we can just move the full SHM page and overlay the ChunkRecord
  // on top of the moved (page + chunk) header.
  // This is checked via static_assert(s) in the .cc file.
  struct ChunkRecord {
    static constexpr uint16_t kWriterIdPadding = 0;
    uint16_t size;  // Size (including the size of the ChunkRecord itself).

    // Unique per Producer (but not within the service).
    // If writer_id == kWriterIdPadding the record should just be skipped.
    uint16_t writer_id;

    uint16_t chunk_id;  // Monotonic counter within the same writer id.
    uint8_t flags;      // see SharedMemoryABI::ChunkHeader::flags.
    uint8_t padding_unused;
    uint64_t producer_id;
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
  const ChunkRecord* GetAlignedPtrForMemcpy(const uint8_t* off) const {
    DcheckIsAlignedAndWithinBounds(off);
    return reinterpret_cast<const ChunkRecord*>(off);
  }

  size_t ReadChunkRecordSizeAt(const uint8_t* off) const {
    const ChunkRecord* aligned_ptr = GetAlignedPtrForMemcpy(off);
    decltype(aligned_ptr->size) size;
    memcpy(&size, &aligned_ptr->size, sizeof(size));

    // We should saturate the size read from the record, to prevent that a
    // corrupted record causes overflows. Right now |size| is a uint16_t and
    // the saturation is implicit. However, if that should change this function
    // (and also ReadChunkRecordAt below) will require an explicit saturation.
    static_assert(sizeof(size) < sizeof(size_t), "Add saturation logic");

    return size;
  }

  void ReadChunkRecordAt(const uint8_t* off, ChunkRecord* chunk_record) const {
    const ChunkRecord* aligned_ptr = GetAlignedPtrForMemcpy(off);
    memcpy(chunk_record, aligned_ptr, sizeof(ChunkRecord));
  }

  // |payload| can be nullptr (in which case payload_size must be == 0), for the
  // case of writing a pading record. In this case the |wptr_| is advanced
  // regularly (accordingly to |record.size|) but no payload is copied.
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

  // Do not keep any metadata about the contents of the buffer outside of
  // the |data_| buffer itself. In future we'll want to be able to resurrect
  // the contents of the buffer from a crash report. The |data_| buffer should
  // be self-describing.

  // TODO(primiano): Move wptr_ inside |data_| somehow. How will we know where
  // to start reading a resurrected buffer?
  base::PageAllocator::UniquePtr data_;
  size_t size_ = 0;

  uint8_t* begin() const { return reinterpret_cast<uint8_t*>(data_.get()); }
  uint8_t* end() const { return begin() + size_; }
  size_t writable_size() const { return end() - wptr_; }

  uint8_t* wptr_ = nullptr;
  uint8_t* rptr_ = nullptr;
};

}  // namespace perfetto

#endif  // SRC_TRACING_CORE_TRACE_BUFFER_H_
