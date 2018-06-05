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

TEST(TraceStorageTest, NoInteractionFirstSched) {
  TraceStorage storage;

  uint32_t cpu = 3;
  uint64_t timestamp = 100;
  uint32_t prev_pid = 2;
  uint32_t prev_state = 32;
  static const char kTestString[] = "test";
  uint32_t next_pid = 4;
  storage.InsertSchedSwitch(cpu, timestamp, prev_pid, prev_state, kTestString,
                            sizeof(kTestString) - 1, next_pid);

  ASSERT_EQ(storage.start_timestamps_for_cpu(cpu), nullptr);
}

TEST(TraceStorageTest, InsertSecondSched) {
  TraceStorage storage;

  uint32_t cpu = 3;
  uint64_t timestamp = 100;
  uint32_t pid_1 = 2;
  uint32_t prev_state = 32;
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  uint32_t pid_2 = 4;
  storage.InsertSchedSwitch(cpu, timestamp, pid_1, prev_state, kCommProc1,
                            sizeof(kCommProc1) - 1, pid_2);
  storage.InsertSchedSwitch(cpu, timestamp + 1, pid_2, prev_state, kCommProc2,
                            sizeof(kCommProc2) - 1, pid_1);

  const auto& timestamps = *storage.start_timestamps_for_cpu(cpu);
  ASSERT_EQ(timestamps.size(), 1ul);
  ASSERT_EQ(timestamps[0], timestamp);
}

TEST(TraceStorageTest, AddProcessEntry) {
  TraceStorage storage;
  storage.AddProcessEntry(1, 1000, "test");
  ASSERT_EQ(storage.UpidsForPid(1)->front(), 0);
  ASSERT_EQ(storage.process_for_upid(0)->time_start, 1000);
}

TEST(TraceStorageTest, AddTwoProcessEntries_SamePid) {
  TraceStorage storage;
  storage.AddProcessEntry(1, 1000, "test");
  storage.AddProcessEntry(1, 2000, "test");
  ASSERT_EQ((*storage.UpidsForPid(1))[0], 0);
  ASSERT_EQ((*storage.UpidsForPid(1))[1], 1);
  ASSERT_EQ(storage.process_for_upid(0)->time_end, 2000);
  ASSERT_EQ(storage.process_for_upid(1)->time_start, 2000);
  ASSERT_EQ(storage.process_for_upid(0)->process_name,
            storage.process_for_upid(1)->process_name);
}

TEST(TraceStorageTest, AddTwoProcessEntries_DifferentPid) {
  TraceStorage storage;
  storage.AddProcessEntry(1, 1000, "test");
  storage.AddProcessEntry(3, 2000, "test");
  ASSERT_EQ((*storage.UpidsForPid(1))[0], 0);
  ASSERT_EQ((*storage.UpidsForPid(3))[0], 1);
  ASSERT_EQ(storage.process_for_upid(1)->time_start, 2000);
}

TEST(TraceStorageTest, UpidsForPid_NonExistantPid) {
  TraceStorage storage;
  ASSERT_EQ(storage.UpidsForPid(1), nullptr);
}

TEST(TraceStorageTest, AddProcessEntry_CorrectName) {
  TraceStorage storage;
  storage.AddProcessEntry(1, 1000, "test");
  ASSERT_EQ((*storage.string_for_string_id(
                storage.process_for_upid(0)->process_name)),
            "test");
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
