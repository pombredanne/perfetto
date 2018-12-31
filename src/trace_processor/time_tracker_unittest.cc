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

#include "src/trace_processor/time_tracker.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

TEST(TimeTrackerUnittest, ClockDomainConversions) {
  TimeTracker tt;
  tt.PushClockSnapshot(ClockDomain::kRealTime, 10, 10010);
  tt.PushClockSnapshot(ClockDomain::kRealTime, 20, 20220);
  tt.PushClockSnapshot(ClockDomain::kRealTime, 30, 30030);
  tt.PushClockSnapshot(ClockDomain::kMonotonic, 1000, 100000);

  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 0), 10000);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 1), 10001);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 9), 10009);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 10), 10010);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 11), 10011);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 19), 10019);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 20), 20220);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 21), 20221);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 29), 20229);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 30), 30030);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kRealTime, 40), 30040);

  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kMonotonic, 0), 100000 - 1000);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kMonotonic, 999), 100000 - 1);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kMonotonic, 1000), 100000);
  EXPECT_EQ(tt.ToTraceTime(ClockDomain::kMonotonic, 1e6), 100000 - 1000 + 1e6);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
