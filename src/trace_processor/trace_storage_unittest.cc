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

#include "src/trace_processor/trace_trace.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;

TEST(TraceStorageTest, AddSliceForCpu) {
  TraceStorage trace;
  trace.AddSliceForCpu(2, 1000, 42, "test");
  ASSERT_EQ(trace.start_timestamps_for_cpu(2)[0], 1000);
}

TEST(TraceStorageTest, AddProcessEntry) {
  TraceStorage trace;
  trace.AddProcessEntry(1, 1000, "test");
  ASSERT_EQ(trace.UpidsForPid(1)->front(), 0);
  ASSERT_EQ(trace.process_for_upid(0)->time_start, 1000);
}

TEST(TraceStorageTest, AddTwoProcessEntries_SamePid) {
  TraceStorage trace;
  trace.AddProcessEntry(1, 1000, "test");
  trace.AddProcessEntry(1, 2000, "test");
  ASSERT_EQ((*trace.UpidsForPid(1))[0], 0);
  ASSERT_EQ((*trace.UpidsForPid(1))[1], 1);
  ASSERT_EQ(trace.process_for_upid(0)->time_end, 2000);
  ASSERT_EQ(trace.process_for_upid(1)->time_start, 2000);
  ASSERT_EQ(trace.process_for_upid(0)->process_name,
            trace.process_for_upid(1)->process_name);
}

TEST(TraceStorageTest, AddTwoProcessEntries_DifferentPid) {
  TraceStorage trace;
  trace.AddProcessEntry(1, 1000, "test");
  trace.AddProcessEntry(3, 2000, "test");
  ASSERT_EQ((*trace.UpidsForPid(1))[0], 0);
  ASSERT_EQ((*trace.UpidsForPid(3))[0], 1);
  ASSERT_EQ(trace.process_for_upid(1)->time_start, 2000);
}

TEST(TraceStorageTest, UpidsForPid_NonExistantPid) {
  TraceStorage trace;
  ASSERT_EQ(trace.UpidsForPid(1), nullptr);
}

TEST(TraceStorageTest, AddProcessEntry_CorrectName) {
  TraceStorage trace;
  trace.AddProcessEntry(1, 1000, "test");
  TraceStorage::StringId id = trace.process_for_upid(0)->process_name;
  ASSERT_EQ((*trace.string_for_string_id(id)), "test");
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
