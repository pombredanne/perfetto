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
#include <thread>
#include <type_traits>
#include <utility>

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
  static constexpr size_t kMaxChunksPerPage = 14;

  // Each TrackePacket in the Chunk is prefixed by 2 bytes stating its size.
  // This limits the max chunk (and in turn, page) size. This does NOT limit
  // the size of a TracePacket, because large packets can still be split across
  // several chunks.
  using PacketHeaderType = uint16_t;
  static constexpr size_t kPacketHeaderSize = sizeof(PacketHeaderType);
  static constexpr size_t kMaxPageSize = 1ul << (8 * kPacketHeaderSize);

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
    // The Chunk is free. The Service shall never touch it, the Producer can
    // acquire it and transition it into kBeingWritten.
    kChunkFree = 0,

    // The Chunk is being used by the Producer and is not complete yet.
    // The Service shall never touch kBeingWritten pages.
    kChunkBeingWritten = 1,

    // The Service is moving the page into its non-shared ring buffer. The
    // Producer shall never touch kBeingRead pages.
    kChunkBeingRead = 2,

    // The Producer is done writing the page and won't touch it again. The
    // Service can now move it to its non-shared ring buffer.
    kChunkComplete = 3,
  };

  enum PageLayout : uint32_t {
    // The page is fully free and has not been partitioned yet.
    kPageNotPartitioned = 0,

    // The page contains (8 == sizeof(PageHeader)):
    // align4(X) := the largest integer N s.t. (N % 4) == 0 && N <= X.

    kPageDiv1 = 1,   // Only one chunk of size: PAGE_SIZE - 8.
    kPageDiv2 = 2,   // Two chunks of size: align4((PAGE_SIZE - 8) / 2).
    kPageDiv4 = 3,   // Four chunks of size: align4((PAGE_SIZE - 8) / 4).
    kPageDiv7 = 4,   // Seven chunks of size: align4((PAGE_SIZE - 8) / 7).
    kPageDiv14 = 5,  // Fourteen chunks of size: align4((PAGE_SIZE - 8) / 14).

    // The rational for 7 and 14 above is to maximize the page usage for the
    // likely case of |page_size| == 4096:
    // (((4096 - 8) / 14) % 4) == 0, while (((4096 - 8) / 16 % 4)) == 3. So
    // Div16 would waste 3 * 16 = 48 bytes per page for chunk alignment gaps.

    kPageDivReserved1 = 6,
    kPageDivReserved2 = 7,
    kNumPageLayouts = 8,
  };

  // Keep this consistent with the enum above.
  static constexpr size_t kNumChunksForLayout[] = {0, 1, 2, 4, 7, 14, 0, 0};

  static constexpr uint32_t kChunkAlignment = 4;
  static constexpr uint32_t kChunkShift = 2;
  static constexpr uint32_t kChunkMask = 0x3;
  static constexpr uint32_t kLayoutMask = 0x70000000;
  static constexpr uint32_t kLayoutShift = 28;
  static constexpr uint32_t kAllChunksMask = 0x0FFFFFFF;

  // This relies on kChunkComplete == 3.
  static constexpr uint32_t kAllChunksComplete = 0x0FFFFFFF;

  struct PageHeader {
    // |layout| bits:
    // [31] [30:29] [28:27] ... [1:0]
    //  |      |       |     |    |
    //  |      |       |     |    +---------- ChunkState[0]
    //  |      |       |     +--------------- ChunkState[12..1]
    //  |      |       +--------------------- ChunkState[13]
    //  |      +----------------------------- PageLayout (0 == page fully free)
    //  +------------------------------------ Reserved for future use
    std::atomic<uint32_t> layout;
    uint32_t reserved;
  };

  struct ChunkHeader {
    struct PacketsState {
      enum Flags : uint8_t {
        // If set, the first TracePacket in the chunk is partial and continues
        // from |chunk_id| - 1 (within the same |writer_id|).
        kFirstPacketContinuesFromPrevChunk = 1 << 0,

        // If set, the last TracePacket in the chunk is partial and continues on
        // |chunk_id| + 1 (within the same |writer_id|).
        kLastPacketContinuesOnNextChunk = 1 << 1,
      };

      uint8_t flags;
      uint8_t reserved;

      // Number of valid TracePacket protobuf messages contained in the chunk.
      // Each TracePacket is prefixed by its own size. This field is
      // monotonically updated by the Producer with release store semantic after
      // the packet has been written into the chunk.
      uint16_t count;
    };

    // This never changes throughout the life of the Chunk.
    struct Identifier {
      // A sequence identifies a linear stream of TracePacket produced by the
      // same data source.
      uint16_t writer_id;

      // chunk_id is a monotonic counter of the chunk within its own
      // sequence. The tuple (writer_id, chunk_id) allows to figure
      // out if two chunks for a data source are contiguous (and hence a trace
      // packet spanning across them can be glues) or we had some holes due to
      // the ring buffer wrapping.
      uint16_t chunk_id;
    };

    // Updated with release-store semantics
    std::atomic<Identifier> identifier;
    std::atomic<PacketsState> packets;
  };

  // TODO would be nicer if the Chunk was move only, purely for doc purposes
  // and to make the Acquire/Release methods look more meaningful.
  class Chunk {
   public:
    void Reset(uintptr_t begin, size_t size);

    void* begin() const { return begin_; }
    uintptr_t begin_addr() const { return reinterpret_cast<uintptr_t>(begin_); }

    void* end() const { return end_; }
    uintptr_t end_addr() const { return reinterpret_cast<uintptr_t>(end_); }

    // Size, including Chunk header.
    size_t size() { return end_addr() - begin_addr(); }

    void* payload_begin() const {
      return reinterpret_cast<void*>(begin_addr() + sizeof(ChunkHeader));
    }

    bool is_valid() const { return begin_ != nullptr && end_ > begin_; }

    ChunkHeader* header() { return reinterpret_cast<ChunkHeader*>(begin_); }

    // Returns the count of packets (|packets.count|) with acquire-load
    // semantics.
    uint16_t GetPacketCount();

    // Increases |packets.count| with release-store semantics. The increment is
    // NOT atomic (i.e. no CAS). Only the Producer is supposed to perform this
    // increment and it is supposed to do this thread-safely (i.e. a Chunk
    // cannot be shared by multiple threads without locking anyways).
    // If |last_packet_is_partial| == true it also toggles the
    // kLastPacketContinuesOnNextChunk flag. The flag update is performed
    // atomically with the |packets.count| update.
    void IncrementPacketCount(bool last_packet_is_partial = false);

   private:
    // This class is deliberately copiable and assignable.
    // Don't add extra fields, that would make the copy expensive.
    void* begin_ = nullptr;
    void* end_ = nullptr;
  };

  SharedMemoryABI(void* start, size_t size, size_t page_size);

  void* start() const { return reinterpret_cast<void*>(start_); }
  void* end() const { return reinterpret_cast<void*>(start_ + size_); }
  size_t size() const { return size_; }
  size_t num_pages() const { return num_pages_; }

  PageHeader* page_header(size_t page_idx) {
    PERFETTO_DCHECK(page_idx < num_pages_);
    return reinterpret_cast<PageHeader*>(start_ + page_size_ * page_idx);
  }

  // Returns true if the page is fully clear and has not been partitioned yet.
  // The state of the page can change at any point after this returns. The
  // caller should use this just as a hint to figure out whether it should
  // TryPartitionPage() or acquire an individual chunk.
  bool is_page_free(size_t page_idx) {
    return page_header(page_idx)->layout.load(std::memory_order_relaxed) == 0;
  }

  // Returns true if all chunks in the page are kChunkComplete. As above, this
  // can change quickly. The service is supposed to
  // TryMarkAllChunksAsBeingRead() if this succeeds.
  bool is_page_complete(size_t page_idx) {
    return (page_header(page_idx)->layout.load(std::memory_order_relaxed) &
            kAllChunksMask) == kAllChunksComplete;
  }

  PageLayout page_layout(size_t page_idx) {
    uint32_t x = page_header(page_idx)->layout.load(std::memory_order_relaxed);
    return static_cast<PageLayout>((x & kLayoutMask) >> kLayoutShift);
  }

  // Returns a bitmap in which each bit is set if the corresponding Chunk exists
  // in the page (according to the page layout) and is free. If the page is not
  // partitioned it returns 0 (as if the page had no free chunks).
  size_t GetFreeChunks(size_t page_idx);

  // Used by the Service to take full ownership of oll the chunks in the a page
  // in one shot.  It tries to atomically migrate all chunks into the
  // kChunkBeingRead state. Can only be done if all chunks are either kChunkFree
  // or kChunkComplete. If this fails, the service has to fall back acquiring
  // the chunks individually.
  bool TryMarkAllChunksAsBeingRead(size_t page_idx);

  // Tries to atomically partition a page with the given |layout|. Returns true
  // if the page was free and has been partitioned; false if the page wasn't
  // free anymore by the time we tried the atomic CAS.
  bool TryPartitionPage(size_t page_idx, PageLayout layout);

  // Tries to atomically mark a single chunk within the page as either
  // kBeingWritten or kBeingRead. Returns false if the page is not partitioned
  // or the chunk is not in a state that allows the transition (respectively,
  // kFree and kComplete). If succeeds, it returns true and updates the Chunk*
  // output argument with the chunk boundaries.
  bool TryAcquireChunkForWrite(size_t page_idx,
                               size_t chunk_idx,
                               const ChunkHeader* chunk_header,
                               Chunk* chunk) {
    return TryAcquireChunk(page_idx, chunk_idx, kChunkBeingWritten,
                           chunk_header, chunk);
  }

  bool TryAcquireChunkForRead(size_t page_idx, size_t chunk_idx, Chunk* chunk) {
    return TryAcquireChunk(page_idx, chunk_idx, kChunkBeingRead, nullptr,
                           chunk);
  }

  // Puts a chunk into the kWriteComplete state.
  void ReleaseChunkAsComplete(Chunk chunk) {
    ReleaseChunk(chunk, kChunkComplete);
  }

  // Puts a chunk into the kChunkFree state.
  void ReleaseChunkAsFree(Chunk chunk) { ReleaseChunk(chunk, kChunkFree); }

  ChunkState GetChunkState(size_t page_idx, size_t chunk_idx);

 private:
  SharedMemoryABI(const SharedMemoryABI&) = delete;
  SharedMemoryABI& operator=(const SharedMemoryABI&) = delete;

  static constexpr size_t GetNumChunksForLayout(uint32_t page_layout) {
    return kNumChunksForLayout[(page_layout & kLayoutMask) >> kLayoutShift];
  }

  size_t GetChunkSizeForPage(uint32_t page_layout) const {
    return chunk_sizes_[(page_layout & kLayoutMask) >> kLayoutShift];
  }

  std::pair<size_t, size_t> GetPageAndChunkIndex(Chunk chunk);

  bool TryAcquireChunk(size_t page_idx,
                       size_t chunk_idx,
                       ChunkState,
                       const ChunkHeader*,
                       Chunk*);
  void ReleaseChunk(Chunk chunk, ChunkState);

  const uintptr_t start_;
  const size_t size_;
  const size_t page_size_;
  const size_t num_pages_;
  std::array<size_t, kNumPageLayouts> const chunk_sizes_;
};

}  // namespace perfetto

#endif  // TRACING_INCLUDE_SHARED_MEMORY_ABI_H_
