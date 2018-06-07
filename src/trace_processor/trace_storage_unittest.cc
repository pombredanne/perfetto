/*
 * Copyright (C) 2017 The Android Open foo Project
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

#include "src/trace_processor/trace_storage.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;

TEST(TraceStorageTest, InsertSecondSched) {
  TraceStorage storage;

  uint32_t cpu = 3;
  uint64_t timestamp = 100;
  uint32_t pid_1 = 2;
  uint32_t prev_state = 32;
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  uint32_t pid_2 = 4;

  const auto& timestamps = storage.SlicesForCpu(cpu).start_ns();
  storage.PushSchedSwitch(cpu, timestamp, pid_1, prev_state, kCommProc1,
                          sizeof(kCommProc1) - 1, pid_2);
  ASSERT_EQ(timestamps.size(), 0);

  storage.PushSchedSwitch(cpu, timestamp + 1, pid_2, prev_state, kCommProc2,
                          sizeof(kCommProc2) - 1, pid_1);

  ASSERT_EQ(timestamps.size(), 1ul);
  ASSERT_EQ(timestamps[0], timestamp);
  ASSERT_EQ(storage.GetThread(0)->start_ns, timestamp);
  ASSERT_EQ(std::string(storage.GetString(storage.GetThread(0)->name)),
            "process1");
}

TEST(TraceStorageTest, AddProcessEntry) {
  TraceStorage storage;
  storage.AddProcessEntry(1, 1000, "test", 4);
  auto pair_it = storage.UpidsForPid(1);
  ASSERT_EQ(pair_it.first->second, 0);
  ASSERT_EQ(storage.GetProcess(0)->start_ns, 1000);
}

TEST(TraceStorageTest, AddTwoProcessEntries_SamePid) {
  TraceStorage storage;
  storage.AddProcessEntry(1, 1000, "test", 4);
  storage.AddProcessEntry(1, 2000, "test", 4);
  auto pair_it = storage.UpidsForPid(1);
  ASSERT_EQ(pair_it.first->second, 0);
  ++pair_it.first;
  ASSERT_EQ(pair_it.first->second, 1);
  ASSERT_EQ(storage.GetProcess(0)->end_ns, 2000);
  ASSERT_EQ(storage.GetProcess(1)->start_ns, 2000);
  ASSERT_EQ(storage.GetProcess(0)->name, storage.GetProcess(1)->name);
}

TEST(TraceStorageTest, AddTwoProcessEntries_DifferentPid) {
  TraceStorage storage;
  storage.AddProcessEntry(1, 1000, "test", 4);
  storage.AddProcessEntry(3, 2000, "test", 4);
  auto pair_it = storage.UpidsForPid(1);
  ASSERT_EQ(pair_it.first->second, 0);
  auto second_pair_it = storage.UpidsForPid(3);
  ASSERT_EQ(second_pair_it.first->second, 1);
  ASSERT_EQ(storage.GetProcess(1)->start_ns, 2000);
}

TEST(TraceStorageTest, UpidsForPid_NonExistantPid) {
  TraceStorage storage;
  auto pair_it = storage.UpidsForPid(1);
  ASSERT_EQ(pair_it.first, pair_it.second);
}

TEST(TraceStorageTest, AddProcessEntry_CorrectName) {
  TraceStorage storage;
  storage.AddProcessEntry(1, 1000, "test", 4);
  ASSERT_EQ(std::string(storage.GetString(storage.GetProcess(0)->name)),
            "test");
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
