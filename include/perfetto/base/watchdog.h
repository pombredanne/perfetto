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

#ifndef INCLUDE_PERFETTO_BASE_WATCHDOG_H_
#define INCLUDE_PERFETTO_BASE_WATCHDOG_H_

#include "perfetto/base/thread_checker.h"

#include <mutex>
#include <thread>

namespace perfetto {
namespace base {

class Watchdog {
 public:
  static const uint32_t kInvalidMemoryLimit;
  static const uint32_t kInvalidCpuPercentage;

  enum TimerReason {
    TASK_DEADLINE = 0,
    TRACE_DEADLINE = 1,
    MAX = TRACE_DEADLINE + 1,
  };

  class TimerHandle {
   public:
    TimerHandle(TimerReason reason);
    ~TimerHandle();
    TimerHandle(TimerHandle&& other) noexcept = default;

   private:
    TimerHandle(const TimerHandle&) = delete;
    TimerHandle& operator=(const TimerHandle&) = delete;

    TimerReason reason_;
  };

  class SlidingWindow {
   public:
    bool Push(uint64_t sample);
    uint64_t Mean() const;
    void Clear();
    void Reset(size_t new_size);

    uint64_t OldestWhenFull() const {
      PERFETTO_CHECK(filled_);
      return window_[position_];
    }

    uint64_t NewestWhenFull() const {
      PERFETTO_CHECK(filled_);
      return window_[(position_ + size_ - 1) % size_];
    }

    size_t size() const { return size_; }

   private:
    bool filled_ = false;
    size_t position_ = 0;
    size_t size_ = 0;
    std::unique_ptr<uint64_t[]> window_;
  };

  static Watchdog* GetInstance();

  TimerHandle CreateFatalTimer(uint32_t ms, TimerReason reason);
  void SetMemoryLimit(uint32_t kb, uint32_t window_ms);
  void SetCpuLimit(uint32_t percentage, uint32_t window_ms);
  void SetPollingTimeForTesting(uint32_t polling_interval_ms);

 private:
  static const uint32_t kPollingIntervalMs;
  static const uint32_t kInvalidTimer;

  struct StatInfo {
    uint64_t cpu_time = 0;
    uint32_t rss_kb = 0;
  };

  Watchdog();
  ~Watchdog() = default;

  [[noreturn]] void ThreadMain();
  void CheckMemory(const StatInfo& stat_info);
  void CheckCpu(const StatInfo& stat_info);
  void CheckTimers();

  void ClearTimer(TimerReason reason);

  StatInfo GetStatInfo();
  uint32_t WindowTimeForSlidingWindow(const SlidingWindow& window);

  std::thread thread_;

  // --- Begin lock-protected members ---

  std::mutex mutex_;

  uint32_t memory_limit_kb_ = kInvalidMemoryLimit;
  SlidingWindow memory_window_kb_;

  uint32_t cpu_limit_percentage_ = kInvalidCpuPercentage;
  SlidingWindow cpu_window_time_;

  uint32_t polling_interval_ms_ = kPollingIntervalMs;
  uint32_t timer_window_countdown_[TimerReason::MAX] = {kInvalidTimer};

  // --- End lock-protected members ---
};

}  // namespace base
}  // namespace perfetto
#endif  // INCLUDE_PERFETTO_BASE_WATCHDOG_H_
