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
  // Create a timer for 20 seconds and don't release wihin the time. We sleep
  // for 26ms because we need to wait an extra 5ms for the watchdog to poll
  // the timer once.
  EXPECT_DEATH(
      {
        TestWatchdog watchdog = TestWatchdog::Create(100);
        auto handle = watchdog.CreateFatalTimer(20);
        usleep(21 * 1000);
      },
      "");
}

TEST(WatchdogTest, NoTimerCrash) {
  // Set a timer for 25ms and release the handle in 20ms.
  TestWatchdog watchdog = TestWatchdog::Create(1);
  auto handle = watchdog.CreateFatalTimer(25);
  PERFETTO_CHECK(usleep(24 * 1000) != -1);
}

// TODO(lalitm): this test does not seem to work as intended and gives very
// high values for RSS.
TEST(WatchdogTest, DISABLED_CrashMemory) {
  EXPECT_DEATH(
      {
        // Allocate 10MB of data and use it to increase RSS.
        uint8_t* ptr = static_cast<uint8_t*>(malloc(10 * 1024 * 1024));
        for (int i = 0; i < 10 * 1024 * 1024 + 1024; i++) {
          ptr[i] = 1;
        }

        TestWatchdog watchdog = TestWatchdog::Create(5);
        watchdog.SetMemoryLimit(10 * 1024 * 1024, 25);

        // Sleep so that the watchdog has some time to pick it up.
        usleep(35 * 1000);
      },
      "");
}

// TODO(lalitm): this test does not seem to work as intended and gives very
// high values for RSS.
TEST(WatchdogTest, DISABLED_NoCrashMemory) {
  TestWatchdog watchdog = TestWatchdog::Create(5);
  watchdog.SetMemoryLimit(10 * 1024 * 1024, 25);

  // Sleep so that the watchdog has some time to pick it up.
  PERFETTO_CHECK(usleep(55 * 1000) != -1);
}

}  // namespace
}  // namespace base
}  // namespace perfetto
