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

#include "perfetto/base/watchdog.h"

#include "perfetto/base/logging.h"

#include "gtest/gtest.h"

#include <time.h>

namespace perfetto {
namespace base {
namespace {

class TestWatchdog : public Watchdog {
 public:
  TestWatchdog(uint32_t polling_interval_ms) : Watchdog(polling_interval_ms) {}
  ~TestWatchdog() override {}
  TestWatchdog(TestWatchdog&& other) noexcept = default;

  static TestWatchdog Create(uint32_t polling_interval_ms) {
    return TestWatchdog(polling_interval_ms);
  }
};

TEST(WatchdogTest, TimerCrash) {
  EXPECT_DEATH(
      {
        TestWatchdog watchdog = TestWatchdog::Create(10);
        watchdog.CreateFatalTimer(50, Watchdog::TimerReason::kTaskDeadline);
        sleep(1);
      },
      "");
}


}  // namespace
}  // namespace base
}  // namespace perfetto
