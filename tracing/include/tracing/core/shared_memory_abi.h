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

#ifndef TRACING_INCLUDE_SHARED_MEMORY_ABI_H_
#define TRACING_INCLUDE_SHARED_MEMORY_ABI_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <atomic>
#include <type_traits>

#include "base/logging.h"

namespace perfetto {

// This file defines the binary interface of the memory buffers shared between
// Producer and Service. This it a long-term stable ABI and has to be backwards
// compatible to deal with mismatching Producer and Service versions.
//
// Overview
// --------
// SMB := "Shared Memory Buffer".
// In the most typical case of a multi-process architecture (i.e. Producer and
// Service are hosted on different processes), a Producer means almost always
// a "client process producing data" (almost: in some cases a process might host
// > 1 Producer, if it links two libraries, independent of each other, that both
// use Perfetto tracing).
// The Service has one SMB for each Producer.
// A producer has one or (typically) more data sources. They all shared the same
// SMB.
// The SMB is a staging area to decouple data sources living in the Producer
// and allow them to do non-blocking async writes.
// The SMB is *not* the ultimate logging buffer seen by the Consumer. That one
// is larger (~MBs) and not shared with Producers.
// Each SMB is small, typically few KB. Its size is configurable by the producer
// within a max limit of ~MB (see kMaxShmSize in service_impl.cc).
// The SMB is partitioned into fixed-size Page(s).
// Page(s) are partitioned by the Producer into variable size Chunk(s).
//
//
// +------------+      +--------------------------+
// | Producer 1 |  <-> |      SMB 1 [~32K - 1MB]  |
// +------------+      +--------+--------+--------+
//                     |  Page  |  Page  |  Page  |
//                     +--------+--------+--------+
//                     | Chunk  |        | Chunk  |
//                     +--------+  Chunk +--------+ <--+
//                     | Chunk  |        | Chunk  |      \
//                     +--------+--------+--------+      +---------------------+
//                                                       |       Service       |
// +------------+      +--------------------------+      +---------------------+
// | Producer 2 |  <-> |      SMB 2 [~32K - 1MB]  |     /| large ring buffers  |
// +------------+      +--------+--------+--------+ <--+ | (100K - several MB) |
//                     |  Page  |  Page  |  Page  |      +---------------------+
//                     +--------+--------+--------+
//                     | Chunk  |        | Chunk  |
//                     +--------+  Chunk +--------+
//                     | Chunk  |        | Chunk  |
//                     +--------+--------+--------+
//
// * Sizes of both SMB and ring buffers are purely indicative and decided at
// configuration time by the Producer (for SMB sizes) and the Consumer (for the
// final ring buffer size).

// Page
// ----
// A page is a portion of the shared memory buffer and defines granularity
// of the interaction between the Producer and tracing Service. When scanning
// the shared memory buffer to determine if something should be moved to the
// central logging buffers, the Service most of the times* looks at and moves
// whole pages. Similarly, the Producer sends an IPC to invite the Service to
// drain the shared memory buffer only when a whole page is filled.
// The page size is essentially a triangular tradeoff between:
// 1) IPC traffic: smaller pages -> more IPCs.
// 2) Proucer lock freedom: larger pages -> larger chunks -> data sources can
//    write more data without needing to swap chunks and synchronize.
// 3) Risk of write-starving the SMB: larger pages -> higher chance that the
//    Service didn't manage to drain them and the SMB remains full.
// The page size, on the other side, has no implications on wasted memory due to
// fragmentations (see Chunk below).
// The size of the page is chosen by the Producer at connection time and stays
// fixed throughout all the lifetime of the Producer. Different producers (i.e.
// ~ different client processes) can chose a different sizes.
// The page size must be an integer multiple of 4k (this is to allow VM page
// stealing optimizations) and obviously has to be an integer divider of the
// total SMB size.

// Chunk
// -----
// A chunk is a portion of a Page which is written and handled by a Producer.
// A chunk contains a linear sequence of TracePacket(s) (the root proto).
// A chunk cannot be written concurrently by two data sources. Protobufs must be
// encoded as contiguous byte streams and cannot be inteleaved. Therefore, on
// the Producer side, a chunk is almost always owned exclusively by one thread
// (% extremely peculiar slow-path cases).
// Chunks are essentially single-writer single-thread lock-free arenas. Locking
// happens only (within the Producer process) when a Chunk is full and a new one
// needs to be acquired.
// The Producer can decide to partition each page into a number of limited
// configurations (e.g., 1 page == 1 chunk, 1 page == 2 chunks,
// 1 page == 8 chunks and so on).

// TracePacket
// -----------
// Is the atom of tracing. Conceptually (putting aside pages and chunks) a trace
// is merely a sequence of TracePacket(s). TracePacket is the root protobuf
// message. A TracePacket can spawn across several chunks (hence even
// across several pages). The Chunks header carries metadata to deal with the
// TracePacket splitting case.

// Use only explicitly-sized types below. DO NOT use size_t or any ABI-dependent
// size. This buffer will be read and written by processes that have a different
// bitness in the same OS.

class SharedMemoryABI {
 public:
  // 14 is the max number that can be encoded in a 32 bit atomic word using
  // 2 state bits per Chunk and leaving 4 bits for the page layout.
  // See PageLayout below.
  constexpr size_t kMaxChunks = 14;

  // Chunk states and transitions:
  //       kFree  <------------------+
  //         |  (Producer)           |
  //         V                       |
  //   kBeingWritten                 |
  //         |  (Producer)           |
  //         V                       |
  //  kWriteComplete                 |
  //         |  (Service)            |
  //         V                       |
  //    kBeingRead                   |
  //        |   (Service)            |
  //        +------------------------+
  enum ChunkState : uint32_t {
    // The chunk is free. The Service will never touch it, the Producer can
    // acquire it and transition it into kBeingWritten.
    kChunkFree = 0,

    // The page is being in use by the Producer and is not complete yet.
    // The Service will never touch kBeingWritten pages.
    kChunkBeingWritten = 1,
    // The Service is moving the page into its non-shared ring buffer. The
    // Producer will never touch kBeingRead pages.
    kChunkBeingRead = 2,

    // The Producer is done writing the page and won't touch it again. The
    // Service can now move it to its non-shared ring buffer.
    kChunkComplete = 3,
  };

  enum PageLayout : uint32_t {
    kPageFree = 0,  // The page is fully free and has not been partitioned yet.

    // The page contains (8 == sizeof(PageHeader)):
    kPageDiv1 = 1,   // Only one chunk of size: PAGE_SIZE - 8.
    kPageDiv2 = 2,   // Two chunks of size: align4((PAGE_SIZE - 8) / 2).
    kPageDiv4 = 3,   // Four chunks of size: align4((PAGE_SIZE - 8) / 4).
    kPageDiv7 = 4,   // Seven chunks of size: align4((PAGE_SIZE - 8) / 7).
    kPageDiv14 = 5,  // Fourteen chunks of size: align4((PAGE_SIZE - 8) / 14).

    kPageReserved1 = 6,
    kPageReserved2 = 7,
  };

  // Once per page at the beginning of the page.
  struct PageHeader {
    // Bits in |layout| are defined as follows:
    // [31] [30:29] [28:27] [26:25] ... [1:0]
    //  |      |       |       |          |
    //  |      |       |       |          +-- ChunkState[0]
    //  |      |       |       +------------- ChunkState[12]
    //  |      |       +--------------------- ChunkState[13]
    //  |      +----------------------------- PageLayout (0 == page fully free)
    //  +------------------------------------ Reserved for future use
    static constexpr uint32_t kChunkMask = 0x3;
    static constexpr uint32_t kLayoutMask = 0xF0000000;
    static constexpr uint32_t kAllChunksMask = 0x0FFFFFFF;
    static constexpr uint32_t AllChunksAre(ChunkState state) {
      return state | state << 2 | state << 4 | state << 6 | state << 8 |
             state << 10 | state << 12 | state << 14 | state << 16 |
             state << 18 | state << 20 | state << 22 | state << 24 |
             state << 26;
    }

    std::atomic<uint32_t> layout;
    uint32_t word2_reserved;
  };

  struct ChunkHeader {
    // A sequence identifies a linear stream of TracePacket produced by the same
    // data source.
    uint16_t sequence_id;

    // chunk_id_in_sequence is a monotonic counter of the chunk within its own
    // sequence. The tuple (sequence_id, chunk_id_in_sequence) allows to figure
    // out if two chunks for a data source are contiguous (and hence a trace
    // packet spanning across them can be glues) or we had some holes due to
    // the ring buffer wrapping.
    uint16_t chunk_id_in_sequence;

    // Number of valid TracePacket protobuf messages contained in the chunk.
    // Each TracePacket is prefixed by its own size.
    uint16_t num_packets;

    // If set, the first TracePacket in the chunk is partial and continues from
    // |chunk_id_in_sequence| - 1 within the same |sequence_id|.
    static constexpr uint8_t kFirstPacketContinuesFromPrevChunk = 1 << 0;

    // If set, the last TracePacket in the chunk is partial and continues on
    // |chunk_id_in_sequence| + 1 within the same |sequence_id|.
    static constexpr uint8_t kLastPacketContinuesOnNextChunk = 1 << 1;

    uint8_t flags;

    uint8_t reserved;
  };

  SharedMemoryABI(void* start, size_t size, size_t page_size);

  void* start() const { return start_; }
  size_t size() const { return size_; }
  size_t num_pages() const { return num_pages_; }

  PageHeader& GetPageHeader(size_t page) {
    PERFETTO_CHECK(page < num_pages_);
    return *(reinterpret_cast<PageHeader*>(start_ + page_size_ * page));
  }

  // Used by the Service to take full ownership of a page in one go.
  // It tries to atomically migrate all chunks into the kChunkBeingRead state.
  // Can only be done if all chunks are either kChunkFree or kChunkComplete.
  // If this fails, the service has to fall back acquiring and moving the
  // chunks individually.
  bool TryMarkAllChunksAsBeingRead(size_t page) {
    PageHeader& page_header = GetPageHeader(page);
    uint32_t state = page_header.layout.load(std::memory_order_relaxed);
    uint32_t next_state = state & PageHeader::kLayoutMask;
    for (size_t i = 0; i < kMaxChunks; i++) {
      ChunkState chunk_state = ((state >> (i * 2)) & PageHeader::kChunkMask);
      switch (chunk_state) {
        case kChunkFree:
        case kChunkComplete:
          next_state |= chunk_state << (i * 2);
          break;
        case kChunkBeingRead:
          // Only the Service can transition chunks into the kChunkBeingRead
          // state. This means that the Service is somehow trying to TryMark
          // twice.
          PERFETTO_DCHECK(false);
        case kChunkBeingWritten:
          return false;
      }
    }
    // Rational for compare_exchange_weak (as opposite to strong): once a chunk
    // is in the kChunkComplete, the write cannot move it back to any other
    // state. Similarly, only the Service can transition chunks into the
    // kChunkFree state. So, no ABA problem can happen, hence weak is fine here.
    return page_header.layout.compare_exchange_weak(state, next_state,
                                                    std::memory_order_acq_rel);
  }

  ChunkHeader& GetChunkHeader(size_t chunk_index);

 private:
  using ChunkSizePerDivider = std::array<size_t, 8>;
  static ChunkSizePerDivider InitChunkSizes(size_t page_size);

  SharedMemoryABI(const SharedMemoryABI&) = delete;
  SharedMemoryABI& operator=(const SharedMemoryABI&) = delete;

  const uintptr_t start_;
  const size_t size_;
  const size_t page_size_;
  const size_t num_pages_;
  ChunkSizePerDivider const chunk_sizes_;
};

}  // namespace perfetto

#endif  // TRACING_INCLUDE_SHARED_MEMORY_ABI_H_
