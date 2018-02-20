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

#include <fcntl.h>
#include <thread>

namespace perfetto {
namespace base {

namespace {

bool IsMultipleOf(uint32_t number, uint32_t divisor) {
  return number >= divisor && number % divisor == 0;
}

uint64_t MeanForArray(uint64_t array[], size_t size) {
  uint64_t total = 0;
  for (size_t i = 0; i < size; i++) {
    total += array[i];
  }
  return total / size;
}

}  //  namespace

const uint32_t Watchdog::kInvalidMemoryLimit = 0;
const uint32_t Watchdog::kInvalidCpuPercentage = 0;
const uint32_t Watchdog::kPollingIntervalMs = 30 * 1000;
const uint32_t Watchdog::kInvalidTimer = 0;

Watchdog::Watchdog() : thread_(&Watchdog::ThreadMain, this) {}

Watchdog* Watchdog::GetInstance() {
  static Watchdog* watchdog = new Watchdog();
  return watchdog;
}

Watchdog::TimerHandle Watchdog::CreateFatalTimer(uint32_t ms,
                                                 TimerReason reason) {
  // std::lock_guard<std::mutex> guard(mutex_);

  PERFETTO_CHECK(IsMultipleOf(ms, polling_interval_ms_));
  PERFETTO_CHECK(timer_window_countdown_[reason] == kInvalidTimer);

  // Update the countdown array with the number of intervals remaining.
  timer_window_countdown_[reason] = ms / polling_interval_ms_;
  return Watchdog::TimerHandle(reason);
}

void Watchdog::SetMemoryLimit(uint32_t kb, uint32_t window_ms) {
  // Update the fields under the lock.
  std::lock_guard<std::mutex> guard(mutex_);
  PERFETTO_CHECK(kb > 0 || kb == kInvalidMemoryLimit);
  PERFETTO_CHECK(IsMultipleOf(window_ms, polling_interval_ms_) ||
                 kb == kInvalidMemoryLimit);

  size_t size =
      kb == kInvalidMemoryLimit ? 0 : window_ms / polling_interval_ms_ + 1;
  memory_window_kb_.Reset(size);
  memory_limit_kb_ = kb;
}

void Watchdog::SetCpuLimit(uint32_t percentage, uint32_t window_ms) {
  // Update the fields under the lock.
  std::lock_guard<std::mutex> guard(mutex_);
  PERFETTO_CHECK(percentage > 0 && percentage <= 100 ||
                 percentage == kInvalidCpuPercentage);
  PERFETTO_CHECK(IsMultipleOf(window_ms, polling_interval_ms_) ||
                 percentage == kInvalidCpuPercentage);

  size_t size = percentage == kInvalidCpuPercentage
                    ? 0
                    : window_ms / polling_interval_ms_ + 1;
  cpu_window_time_.Reset(size);
  cpu_limit_percentage_ = percentage;
}

void Watchdog::SetPollingTimeForTesting(uint32_t polling_interval_ms) {
  std::lock_guard<std::mutex> guard(mutex_);
  polling_interval_ms_ = polling_interval_ms;

  // If we reset the polling interval, all our sliding windows and timers no
  // longer make sense so just clear them all.
  memory_window_kb_.Clear();
  cpu_window_time_.Clear();
  for (size_t i = 0; i < TimerReason::MAX; i++) {
    timer_window_countdown_[i] = kInvalidTimer;
  }
}

void Watchdog::ClearTimer(TimerReason reason) {
  std::lock_guard<std::mutex> guard(mutex_);
  timer_window_countdown_[reason] = kInvalidTimer;
}

void Watchdog::ThreadMain() {
  /*
  std::unique_lock<std::mutex> guard(mutex_);
  std::condition_variable var;
  for (;;) {
    // Release the lock and wait for the polling interval amount of time.
    var.wait_for(guard, std::chrono::milliseconds(polling_interval_ms_));

    // Check each of our possible fatal conditions.
    StatInfo stat_info = GetStatInfo();
    CheckMemory(stat_info);
    CheckCpu(stat_info);
    CheckTimers();
  }
  */
  for (;;) {
  }
}

void Watchdog::CheckMemory(const StatInfo& stat_info) {
  if (memory_limit_kb_ == kInvalidMemoryLimit)
    return;

  if (memory_window_kb_.Push(stat_info.rss_kb))
    PERFETTO_CHECK(memory_window_kb_.Mean() <= memory_limit_kb_);
}

void Watchdog::CheckCpu(const StatInfo& stat_info) {
  if (cpu_limit_percentage_ == kInvalidMemoryLimit)
    return;

  if (cpu_window_time_.Push(stat_info.cpu_time)) {
    uint64_t difference = static_cast<uint64_t>(
        cpu_window_time_.NewestWhenFull() - cpu_window_time_.OldestWhenFull());
    uint64_t percentage =
        difference / WindowTimeForSlidingWindow(cpu_window_time_);
    PERFETTO_CHECK(percentage <= cpu_limit_percentage_);
  }
}

void Watchdog::CheckTimers() {
  for (size_t i = 0; i < TRACE_DEADLINE; i++) {
    if (timer_window_countdown_[i] == kInvalidTimer)
      continue;

    // If we hit a timer of 0 and we haven't had the flag cleared, then
    // we should abort.
    PERFETTO_CHECK(timer_window_countdown_[i] != 0);

    // Otherwise decrement the counter.
    timer_window_countdown_[i]--;
  }
}

Watchdog::StatInfo Watchdog::GetStatInfo() {
  Watchdog::StatInfo stat_info;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // Open the stat file.
  FILE* file = fopen("/proc/self/stat", "r");
  PERFETTO_CHECK(file);

  // Read the data we want into the struct.
  unsigned long utime = 0;
  unsigned long stime = 0;
  long rss_pages = -1l;
  PERFETTO_CHECK(
      fscanf(file,
             "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu %lu %lu "
             "%*ld %*ld %*ld %*ld %*ld %*ld %*llu %*lu %ld",
             &utime, &stime, &rss_pages) == 3);
  perfetto::base::ignore_result(fclose(file));

  stat_info.cpu_time = utime + stime;
  stat_info.rss_kb = static_cast<uint32_t>(rss_pages) * 4096;
#endif

  return stat_info;
}

uint32_t Watchdog::WindowTimeForSlidingWindow(const SlidingWindow& window) {
  return static_cast<uint32_t>(window.size() - 1) * polling_interval_ms_;
}

bool Watchdog::SlidingWindow::Push(uint64_t sample) {
  // Add the sample to the current position in the ring buffer.
  window_[position_] = sample;

  // Update the position with next one circularily.
  position_ = (position_ + 1) % size_;

  // Set the filled flag the first time we wrap.
  filled_ = filled_ || position_ == 0;
  return filled_;
}

uint64_t Watchdog::SlidingWindow::Mean() const {
  return MeanForArray(window_.get(), size_);
}

void Watchdog::SlidingWindow::Clear() {
  position_ = 0;
  window_.reset(new uint64_t[size_]());
}

void Watchdog::SlidingWindow::Reset(size_t new_size) {
  PERFETTO_CHECK(new_size >= 0);
  position_ = 0;
  size_ = new_size;
  window_.reset(new_size == 0 ? nullptr : new uint64_t[new_size]());
}

Watchdog::TimerHandle::TimerHandle(TimerReason reason) : reason_(reason) {}
Watchdog::TimerHandle::~TimerHandle() {
  Watchdog::GetInstance()->ClearTimer(reason_);
}

}  // namespace base
}  // namespace perfetto
