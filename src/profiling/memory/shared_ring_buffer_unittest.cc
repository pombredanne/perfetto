/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/profiling/memory/shared_ring_buffer.h"

#include <array>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>

#include "gtest/gtest.h"
#include "perfetto/base/optional.h"

namespace perfetto {
namespace profiling {
namespace {

std::string ToString(const SharedRingBuffer::BufferAndSize& buf_and_size) {
  return std::string(reinterpret_cast<const char*>(&buf_and_size.payload()[0]),
                     buf_and_size.payload_size());
}

void StructuredTest(SharedRingBuffer* wr, SharedRingBuffer* rd) {
  ASSERT_TRUE(wr);
  ASSERT_TRUE(wr->is_valid());
  ASSERT_TRUE(wr->size() == rd->size());
  const size_t buf_size = wr->size();

  // Test small writes.
  ASSERT_TRUE(wr->TryWrite("foo", 4));
  ASSERT_TRUE(wr->TryWrite("bar", 4));

  {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(buf_and_size.payload_size(), 4);
    ASSERT_STREQ(reinterpret_cast<const char*>(&buf_and_size.payload()[0]),
                 "foo");
  }
  {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(buf_and_size.payload_size(), 4);
    ASSERT_STREQ(reinterpret_cast<const char*>(&buf_and_size.payload()[0]),
                 "bar");
  }

  for (int i = 0; i < 3; i++) {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(buf_and_size.payload(), nullptr);
    ASSERT_EQ(buf_and_size.payload_size(), 0);
  }

  // Test extremely large writes (fill the buffer)
  for (int i = 0; i < 3; i++) {
    // TryWrite precisely |buf_size| bytes (minus the size header itself).
    std::string data(buf_size - sizeof(uint64_t), '.' + static_cast<char>(i));
    ASSERT_TRUE(wr->TryWrite(data.data(), data.size()));
    ASSERT_FALSE(wr->TryWrite(data.data(), data.size()));
    ASSERT_FALSE(wr->TryWrite("?", 1));

    // And read it back
    auto buf_and_size = rd->Read();
    ASSERT_EQ(ToString(buf_and_size), data);
  }

  // Test large writes that wrap.
  std::string data(buf_size / 4 * 3 - sizeof(uint64_t), '!');
  ASSERT_TRUE(wr->TryWrite(data.data(), data.size()));
  ASSERT_FALSE(wr->TryWrite(data.data(), data.size()));
  {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(ToString(buf_and_size), data);
  }
  data = std::string(base::kPageSize - sizeof(uint64_t), '#');
  for (int i = 0; i < 4; i++)
    ASSERT_TRUE(wr->TryWrite(data.data(), data.size()));

  for (int i = 0; i < 4; i++) {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(buf_and_size.payload_size(), data.size());
    ASSERT_EQ(ToString(buf_and_size), data);
  }

  // Test misaligned writes.
  ASSERT_TRUE(wr->TryWrite("1", 1));
  ASSERT_TRUE(wr->TryWrite("22", 2));
  ASSERT_TRUE(wr->TryWrite("333", 3));
  ASSERT_TRUE(wr->TryWrite("55555", 5));
  ASSERT_TRUE(wr->TryWrite("7777777", 7));
  {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(ToString(buf_and_size), "1");
  }
  {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(ToString(buf_and_size), "22");
  }
  {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(ToString(buf_and_size), "333");
  }
  {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(ToString(buf_and_size), "55555");
  }
  {
    auto buf_and_size = rd->Read();
    ASSERT_EQ(ToString(buf_and_size), "7777777");
  }
}

TEST(SharedRingBufferTest, SingleThreadSameInstance) {
  constexpr auto kBufSize = base::kPageSize * 4;
  base::Optional<SharedRingBuffer> buf = SharedRingBuffer::Create(kBufSize);
  StructuredTest(&*buf, &*buf);
}

TEST(SharedRingBufferTest, SingleThreadAttach) {
  constexpr auto kBufSize = base::kPageSize * 4;
  base::Optional<SharedRingBuffer> buf1 = SharedRingBuffer::Create(kBufSize);
  base::Optional<SharedRingBuffer> buf2 =
      SharedRingBuffer::Attach(base::ScopedFile(dup(buf1->fd())));
  StructuredTest(&*buf1, &*buf2);
}

}  // namespace
}  // namespace profiling
}  // namespace perfetto
