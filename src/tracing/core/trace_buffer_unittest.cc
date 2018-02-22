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

#include <string.h>

#include <random>
#include <vector>

#include "perfetto/protozero/proto_utils.h"
#include "perfetto/tracing/core/basic_types.h"
#include "perfetto/tracing/core/shared_memory_abi.h"
#include "src/tracing/core/trace_buffer.h"

#include "gtest/gtest.h"

namespace perfetto {
namespace {

constexpr uint8_t kFirstPacketContinuesFromPrevChunk =
    SharedMemoryABI::ChunkHeader::kFirstPacketContinuesFromPrevChunk;
constexpr uint8_t kLastPacketContinuesOnNextChunk =
    SharedMemoryABI::ChunkHeader::kLastPacketContinuesOnNextChunk;

struct FakeChunk {
  FakeChunk(ProducerID p, WriterID w, ChunkID c)
      : producer_id{p}, writer_id{w}, chunk_id{c} {}

  void AddPacket(size_t size,
                 char seed,
                 bool continues_from_prev = false,
                 bool continues_on_next = false) {
    if (continues_from_prev) {
      PERFETTO_CHECK(num_packets == 0);
      flags |= kFirstPacketContinuesFromPrevChunk;
    }
    PERFETTO_CHECK(!(flags & kLastPacketContinuesOnNextChunk));
    if (continues_on_next) {
      flags |= kLastPacketContinuesOnNextChunk;
    }
    num_packets++;

    uint8_t size_varint[8]{};
    uint8_t* end = protozero::proto_utils::WriteVarInt(size, size_varint);
    data.insert(data.end(), size_varint, end);
    std::default_random_engine rnd_engine(seed);
    for (size_t i = 0; i < size; i++)
      data.push_back(static_cast<uint8_t>(rnd_engine()));
  }

  void CopyInto(TraceBuffez* tb) {
    tb->CopyChunkUntrusted(producer_id, writer_id, chunk_id, num_packets, flags,
                           data.data(), data.size());
  }

  size_t size() const { return data.size(); }

  ProducerID producer_id;
  WriterID writer_id;
  ChunkID chunk_id;
  uint8_t flags = 0;
  uint16_t num_packets = 0;
  std::vector<uint8_t> data;
};

bool ReadPacket(TraceBuffez* tb, size_t expected_size = 0, char seed = 0) {
  Slices slices;
  if (!tb->ReadNextTracePacket(&slices))
    return false;
  EXPECT_EQ(1u, slices.size());
  const Slice& slice = slices[0];
  EXPECT_EQ(expected_size, slice.size);
  if (expected_size != slice.size)
    return false;
  std::default_random_engine rnd_engine(seed);
  for (size_t i = 0; i < expected_size; i++) {
    uint8_t actual = reinterpret_cast<const uint8_t*>(slice.start)[i];
    uint8_t expected = static_cast<uint8_t>(rnd_engine());
    EXPECT_EQ(expected, actual);
    if (expected != actual)
      return false;
  }
  return true;
}

// On each iteration writes a fixed-size chunk and reads it back.
TEST(TraceBufferTest, OneWriteOneRead_OneStream_NoFragments) {
  TraceBuffez tb;
  ASSERT_TRUE(tb.Create(64 * 1024));
  for (ChunkID chunk_id = 0; chunk_id < 1000; chunk_id++) {
    FakeChunk fc(ProducerID(1), WriterID(1), chunk_id);
    char seed = static_cast<char>(chunk_id);
    fc.AddPacket(20, seed);
    fc.CopyInto(&tb);

    tb.BeginRead();
    ASSERT_TRUE(ReadPacket(&tb, 20, seed));
    ASSERT_FALSE(ReadPacket(&tb));
  }
}

// On each iteration writes a variable number of packets of variable size.
TEST(TraceBufferTest, VariableSizePackets_OneStream_NoFragments) {
  TraceBuffez tb;
  ASSERT_TRUE(tb.Create(64 * 1024));
  auto GetNumPackets = [](ChunkID chunk_id) -> int { return chunk_id % 4 + 1; };
  auto GetSeed = [](ChunkID chunk_id, int packet_num) {
    return static_cast<char>(chunk_id * 121 + packet_num);
  };

  const ChunkID kNumChunks = 100;
  for (ChunkID chunk_id = 0; chunk_id < kNumChunks; chunk_id++) {
    FakeChunk fc(ProducerID(1), WriterID(1), chunk_id);
    for (int p = 0; p < GetNumPackets(chunk_id); p++)
      fc.AddPacket(20 + p * 5, GetSeed(chunk_id, p));
    fc.CopyInto(&tb);
  }

  tb.BeginRead();
  for (ChunkID chunk_id = 0; chunk_id < kNumChunks; chunk_id++) {
    for (int p = 0; p < GetNumPackets(chunk_id); p++)
      ASSERT_TRUE(ReadPacket(&tb, 20 + p * 5, GetSeed(chunk_id, p)));
  }
  ASSERT_FALSE(ReadPacket(&tb, 0));
}

// TODO test iterator basic logic
// TODO test iterator wrapping logic
// TODO test stitching
// TODO test patching
// TODO test padding
// TODO test stats
// TODO test multiple streams
// TODO test long packet
// TODO test OOO chunks don't block / fill up
// TODO test malicious packets
// TODO: case of exactly sizeof(ChunkRecord) bytes left at the end of the buf.

}  // namespace
}  // namespace perfetto
