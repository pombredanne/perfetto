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

#include "perfetto/tracing/core/shared_memory_abi.h"

#include "gtest/gtest.h"

namespace perfetto {
namespace {

using Chunk = SharedMemoryABI::Chunk;
using ChunkHeader = SharedMemoryABI::ChunkHeader;

std::unique_ptr<uint8_t[]> GetBuffer(size_t size) {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[size]);
  memset(buf.get(), 0, size);
  return buf;
}

TEST(SharedMemoryABITest, SingleThreaded) {
  const size_t kPageSize = 4096;
  const size_t kNumPages = 10;
  const size_t kBufSize = kPageSize * kNumPages;
  std::unique_ptr<uint8_t[]> buf = GetBuffer(kBufSize);
  SharedMemoryABI abi(buf.get(), kBufSize, kPageSize);

  ASSERT_EQ(buf.get(), abi.start());
  ASSERT_EQ(buf.get() + kBufSize, abi.end());
  ASSERT_EQ(kBufSize, abi.size());
  ASSERT_EQ(kPageSize, abi.page_size());
  ASSERT_EQ(kNumPages, abi.num_pages());

  for (size_t i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(abi.is_page_free(i));
    ASSERT_FALSE(abi.is_page_complete(i));
    // GetFreeChunks() should return 0 for an unpartitioned page.
    ASSERT_EQ(0u, abi.GetFreeChunks(i));
  }

  ASSERT_TRUE(abi.TryPartitionPage(0, SharedMemoryABI::kPageDiv1, 10));
  ASSERT_EQ(0x01u, abi.GetFreeChunks(0));

  ASSERT_TRUE(abi.TryPartitionPage(1, SharedMemoryABI::kPageDiv2, 11));
  ASSERT_EQ(0x03u, abi.GetFreeChunks(1));

  ASSERT_TRUE(abi.TryPartitionPage(2, SharedMemoryABI::kPageDiv4, 12));
  ASSERT_EQ(0x0fu, abi.GetFreeChunks(2));

  ASSERT_TRUE(abi.TryPartitionPage(3, SharedMemoryABI::kPageDiv7, 13));
  ASSERT_EQ(0x7fu, abi.GetFreeChunks(3));

  ASSERT_TRUE(abi.TryPartitionPage(4, SharedMemoryABI::kPageDiv14, 14));
  ASSERT_EQ(0x3fffu, abi.GetFreeChunks(4));

  // Repartitioning an existing page must fail.
  ASSERT_FALSE(abi.TryPartitionPage(0, SharedMemoryABI::kPageDiv1, 10));
  ASSERT_FALSE(abi.TryPartitionPage(4, SharedMemoryABI::kPageDiv14, 14));

  for (size_t i = 0; i <= 4; i++) {
    ASSERT_FALSE(abi.is_page_free(i));
    ASSERT_FALSE(abi.is_page_complete(i));
  }

  uint16_t last_chunk_id = 0;
  unsigned last_writer_id = 0;
  uint8_t* last_chunk_begin = nullptr;
  uint8_t* last_chunk_end = nullptr;

  for (size_t page_idx = 0; page_idx <= 4; page_idx++) {
    const size_t num_chunks =
        SharedMemoryABI::GetNumChunksForLayout(abi.page_layout_dbg(page_idx));
    const size_t target_buffer = 10 + page_idx;

    for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
      ChunkHeader header{};

      uint16_t chunk_id = ++last_chunk_id;
      last_writer_id = (last_writer_id + 1) & SharedMemoryABI::kMaxWriterID;
      unsigned writer_id = last_writer_id;
      header.identifier.store({chunk_id, writer_id, 0 /* reserved */});

      uint16_t packets_count = static_cast<uint16_t>(chunk_idx * 10);
      uint8_t flags = static_cast<uint8_t>(0xffu - chunk_idx);
      header.packets.store({packets_count, flags, 0 /* reserved */});

      // Acquiring a chunk with a different target_buffer should fail.
      Chunk chunk = abi.TryAcquireChunkForWriting(page_idx, chunk_idx,
                                                  target_buffer + 1, &header);
      ASSERT_FALSE(chunk.is_valid());

      // But acquiring with the right |target_buffer| should succeed.
      chunk = abi.TryAcquireChunkForWriting(page_idx, chunk_idx, target_buffer,
                                            &header);
      ASSERT_TRUE(chunk.is_valid());
      size_t expected_chunk_size =
          (kPageSize - sizeof(SharedMemoryABI::PageHeader)) / num_chunks;
      expected_chunk_size = expected_chunk_size - (expected_chunk_size % 4);
      ASSERT_EQ(expected_chunk_size, chunk.size());
      ASSERT_GT(chunk.begin(), last_chunk_begin);
      ASSERT_GE(chunk.begin(), last_chunk_end);
      ASSERT_GT(chunk.end(), chunk.begin());
      ASSERT_EQ(chunk.end(), chunk.begin() + chunk.size());
      last_chunk_begin = chunk.begin();
      last_chunk_end = chunk.end();

      // TODO cover chunk begin, end size.
      EXPECT_EQ(chunk_id, chunk.header()->identifier.load().chunk_id);
      EXPECT_EQ(writer_id, chunk.header()->identifier.load().writer_id);
      EXPECT_EQ(packets_count, chunk.header()->packets.load().count);
      EXPECT_EQ(flags, chunk.header()->packets.load().flags);
      EXPECT_EQ(std::make_pair(packets_count, flags),
                chunk.GetPacketCountAndFlags());

      // Reacquiring the same chunk should fail.
      chunk = abi.TryAcquireChunkForWriting(page_idx, chunk_idx, target_buffer,
                                            &header);
      ASSERT_FALSE(chunk.is_valid());
    }
  }
}

}  // namespace
}  // namespace perfetto
