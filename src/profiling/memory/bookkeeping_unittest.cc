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

#include "src/profiling/memory/bookkeeping.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace {

std::vector<MemoryBookkeeping::CodeLocation> stack() {
  return {
      {"map1", "fun1"}, {"map2", "fun2"},
  };
}

std::vector<MemoryBookkeeping::CodeLocation> stack2() {
  return {
      {"map1", "fun1"}, {"map3", "fun3"},
  };
}

TEST(BookkeepingTest, Basic) {
  uint64_t sequence_number = 1;
  MemoryBookkeeping mb;
  mb.RecordMalloc(stack(), 1, 5, sequence_number++);
  mb.RecordMalloc(stack2(), 2, 2, sequence_number++);
  ASSERT_EQ(mb.GetCumSizeForTesting({{"map1", "fun1"}}), 7);
  mb.RecordFree(2, sequence_number++);
  ASSERT_EQ(mb.GetCumSizeForTesting({{"map1", "fun1"}}), 5);
  mb.RecordFree(1, sequence_number++);
  ASSERT_EQ(mb.GetCumSizeForTesting({{"map1", "fun1"}}), 0);
}

TEST(BookkeepingTest, ReplaceAlloc) {
  uint64_t sequence_number = 1;
  MemoryBookkeeping mb;
  mb.RecordMalloc(stack(), 1, 5, sequence_number++);
  mb.RecordMalloc(stack2(), 1, 2, sequence_number++);
  EXPECT_EQ(mb.GetCumSizeForTesting(stack()), 0);
  EXPECT_EQ(mb.GetCumSizeForTesting(stack2()), 2);
}

TEST(BookkeepingTest, OutOfOrder) {
  MemoryBookkeeping mb;
  mb.RecordMalloc(stack(), 1, 5, 1);
  mb.RecordMalloc(stack2(), 1, 2, 0);
  EXPECT_EQ(mb.GetCumSizeForTesting(stack()), 5);
  EXPECT_EQ(mb.GetCumSizeForTesting(stack2()), 0);
}

}  // namespace
}  // namespace perfetto
