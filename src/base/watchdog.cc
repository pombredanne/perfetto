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
#include "perfetto/base/scoped_file.h"

#include <fcntl.h>
#include <thread>

namespace perfetto {
namespace base {

namespace {

static constexpr uint32_t kDefaultPollingInterval = 30 * 1000;

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

Watchdog::Watchdog(uint32_t polling_interval_ms)
    : thread_(&Watchdog::ThreadMain, this),
      polling_interval_ms_(polling_interval_ms) {
  thread_.detach();
}

Watchdog* Watchdog::GetInstance() {
  static Watchdog* watchdog = new Watchdog(kDefaultPollingInterval);
  return watchdog;
}

Watchdog::TimerHandle Watchdog::CreateFatalTimer(uint32_t ms,
                                                 TimerReason reason) {
  std::lock_guard<std::mutex> guard(mutex_);

  PERFETTO_CHECK(IsMultipleOf(ms, polling_interval_ms_));
  PERFETTO_CHECK(timer_window_countdown_[reason] == 0);

  // Update the countdown array with the number of intervals remaining + 1.
  timer_window_countdown_[reason] =
      static_cast<int32_t>(ms / polling_interval_ms_) + 1;
  return Watchdog::TimerHandle(reason);
}

void Watchdog::SetMemoryLimit(uint32_t kb, uint32_t window_ms) {
  // Update the fields under the lock.
  std::lock_guard<std::mutex> guard(mutex_);

  PERFETTO_CHECK(IsMultipleOf(window_ms, polling_interval_ms_) || kb == 0);

  size_t size = kb == 0 ? 0 : window_ms / polling_interval_ms_ + 1;
  memory_window_kb_.Reset(size);
  memory_limit_kb_ = kb;
}

void Watchdog::SetCpuLimit(uint32_t percentage, uint32_t window_ms) {
  std::lock_guard<std::mutex> guard(mutex_);

  PERFETTO_CHECK(percentage <= 100);
  PERFETTO_CHECK(IsMultipleOf(window_ms, polling_interval_ms_) ||
                 percentage == 0);

  size_t size = percentage == 0 ? 0 : window_ms / polling_interval_ms_ + 1;
  cpu_window_time_.Reset(size);
  cpu_limit_percentage_ = percentage;
}

void Watchdog::ClearTimer(TimerReason reason) {
  std::lock_guard<std::mutex> guard(mutex_);
  PERFETTO_DCHECK(timer_window_countdown_[reason] != 0);
  timer_window_countdown_[reason] = 0;
}

void Watchdog::ThreadMain() {
  /*
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  base::ScopedFstream file(fopen("/proc/self/stat", "r"));
  if (!file)
    PERFETTO_ELOG("Failed to open stat file to enforce resource limits.");
#endif

  for (;;) {
    // Sleep for the polling interval amount of useconds.
    usleep(polling_interval_ms_ * 1000);

    std::unique_lock<std::mutex> guard(mutex_);

    uint64_t cpu_time = 0;
    uint64_t rss_kb = 0;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    if (file) {
      unsigned long utime = 0;
      unsigned long stime = 0;
      long rss_pages = -1l;

      // Read the file from the beginning for utime, stime and rss.
      rewind(file.get());
      PERFETTO_CHECK(
          fscanf(
              file.get(),
              "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu %lu %lu "
              "%*ld %*ld %*ld %*ld %*ld %*ld %*llu %*lu %ld",
              &utime, &stime, &rss_pages) == 3);

      cpu_time = utime + stime;
      rss_kb = static_cast<uint32_t>(rss_pages) * base::kPageSize;
    }
#endif

    CheckMemory(rss_kb);
    CheckCpu(cpu_time);
    CheckTimers();
  }
  */
  return;
}

void Watchdog::CheckMemory(uint64_t rss_kb) {
  if (memory_limit_kb_ == 0)
    return;

  // Add the current stat value to the ring buffer and check that the mean
  // remains under our threshold.
  if (memory_window_kb_.Push(rss_kb))
    PERFETTO_CHECK(memory_window_kb_.Mean() <= memory_limit_kb_);
}

void Watchdog::CheckCpu(uint64_t cpu_time) {
  if (cpu_limit_percentage_ == 0)
    return;

  // Add the cpu time to the ring buffer.
  if (cpu_window_time_.Push(cpu_time)) {
    // Compute the percentage over the whole window and check that it remains
    // under the threshold.
    uint64_t difference = static_cast<uint64_t>(
        cpu_window_time_.NewestWhenFull() - cpu_window_time_.OldestWhenFull());
    uint64_t percentage =
        difference / WindowTimeForRingBuffer(cpu_window_time_);
    PERFETTO_CHECK(percentage <= cpu_limit_percentage_);
  }
}

void Watchdog::CheckTimers() {
  for (size_t i = 0; i < TimerReason::kMax; i++) {
    if (timer_window_countdown_[i] == 0)
      continue;

    // If we hit a timer of 1 and we haven't had the flag cleared, then
    // we should crash the program.
    PERFETTO_CHECK(timer_window_countdown_[i] != 1);

    // Otherwise decrement the counter.
    timer_window_countdown_[i]--;
  }
}

uint32_t Watchdog::WindowTimeForRingBuffer(const WindowedInterval& window) {
  return static_cast<uint32_t>(window.size() - 1) * polling_interval_ms_;
}

bool Watchdog::WindowedInterval::Push(uint64_t sample) {
  // Add the sample to the current position in the ring buffer.
  buffer_[position_] = sample;

  // Update the position with next one circularily.
  position_ = (position_ + 1) % size_;

  // Set the filled flag the first time we wrap.
  filled_ = filled_ || position_ == 0;
  return filled_;
}

uint64_t Watchdog::WindowedInterval::Mean() const {
  return MeanForArray(buffer_.get(), size_);
}

void Watchdog::WindowedInterval::Clear() {
  position_ = 0;
  buffer_.reset(new uint64_t[size_]());
}

void Watchdog::WindowedInterval::Reset(size_t new_size) {
  PERFETTO_CHECK(new_size >= 0);
  position_ = 0;
  size_ = new_size;
  buffer_.reset(new_size == 0 ? nullptr : new uint64_t[new_size]());
}

Watchdog::TimerHandle::TimerHandle(TimerReason reason) : reason_(reason) {}
Watchdog::TimerHandle::~TimerHandle() {
  Watchdog::GetInstance()->ClearTimer(reason_);
}
Watchdog::TimerHandle::TimerHandle(TimerHandle&& other) = default;

}  // namespace base
}  // namespace perfetto
