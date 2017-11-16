/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "base/task_runner_posix.h"

#include <poll.h>
#include <unistd.h>

namespace perfetto {
namespace base {

TaskRunnerPosix::TaskRunnerPosix() {
  int pipe_fds[2];
  if (pipe(pipe_fds) == -1) {
    PERFETTO_DPLOG("pipe()");
    return;
  }
  control_read_.reset(pipe_fds[0]);
  control_write_.reset(pipe_fds[1]);
  pending_poll_tasks_[control_read_.get()] = [] {};
}

TaskRunnerPosix::~TaskRunnerPosix() = default;

TaskRunnerPosix::TimePoint TaskRunnerPosix::GetTime() const {
  return std::chrono::steady_clock::now();
}

void TaskRunnerPosix::WakeUp() {
  // If we're running on the main thread there's no need to schedule a wake-up.
  if (thread_checker_.CalledOnValidThread())
    return;
  if (write(control_write_.get(), nullptr, 0) < 0)
    PERFETTO_DPLOG("write()");
}

void TaskRunnerPosix::Run() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  while (true) {
    int next_task_delay_ms;
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (done_)
        break;
      next_task_delay_ms = static_cast<int>(GetDelayToNextTaskLocked().count());
      UpdatePollTasksLocked();
    }
    int ret = 0;
    // Don't start polling until we run out of runnable tasks.
    if (next_task_delay_ms) {
      ret = poll(&poll_fds_[0], static_cast<nfds_t>(poll_fds_.size()),
                 next_task_delay_ms);
    }
    if (ret == -1) {
      PERFETTO_DPLOG("poll()");
      return;
    } else if (ret == 0) {
      // Timeout.
      RunImmediateTask();
      RunDelayedTask();
    } else {
      RunImmediateTask();
      RunFileDescriptorWatches();
      RunDelayedTask();
    }
  }
}

void TaskRunnerPosix::Quit() {
  {
    std::lock_guard<std::mutex> lock(lock_);
    done_ = true;
  }
  WakeUp();
}

void TaskRunnerPosix::UpdatePollTasksLocked() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!poll_tasks_changed_)
    return;
  poll_tasks_changed_ = false;

  // Note that we need to maintain the state of unmodified fds in |poll_fds_|
  // to avoid missing events.

  // Remove unregistered fds or update changed fds.
  for (size_t i = 0; i < poll_fds_.size(); i++) {
    auto it = pending_poll_tasks_.find(poll_fds_[i].fd);
    if (it == pending_poll_tasks_.end())
      continue;
    if (!it->second) {
      // Unregistered fd.
      poll_fds_.erase(poll_fds_.begin() + static_cast<ssize_t>(i));
      poll_tasks_.erase(poll_tasks_.begin() + static_cast<ssize_t>(i));
      i--;
      continue;
    } else {
      // Changed fd.
      poll_tasks_[i] = std::move(it->second);
    }
  }

  // Add registered fds.
  for (const auto& it : pending_poll_tasks_) {
    if (!it.second)
      continue;
    poll_fds_.push_back({it.first, POLLIN, 0});
    poll_tasks_.push_back(std::move(it.second));
  }

  pending_poll_tasks_.clear();
}

void TaskRunnerPosix::RunImmediateTask() {
  if (immediate_tasks_.empty())
    return;
  auto task = std::move(immediate_tasks_.front());
  immediate_tasks_.pop_front();
  task();
}

void TaskRunnerPosix::RunDelayedTask() {
  if (delayed_tasks_.empty())
    return;
  auto it = delayed_tasks_.begin();
  if (GetTime() < it->first)
    return;
  auto task = std::move(it->second);
  delayed_tasks_.erase(it);
  task();
}

void TaskRunnerPosix::RunFileDescriptorWatches() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  bool should_update = true;
  for (size_t i = 0; i < poll_fds_.size(); i++) {
    // Any task may change the set of fds we're interested in, so update the
    // set every time we run a task.
    if (should_update) {
      std::lock_guard<std::mutex> lock(lock_);
      UpdatePollTasksLocked();
      should_update = false;
    }
    if (i >= poll_fds_.size())
      break;
    if (!(poll_fds_[i].revents & POLLIN))
      continue;
    poll_fds_[i].revents = 0;
    should_update = true;
    const auto& task = poll_tasks_[i];
    task();
  }
}

TaskRunnerPosix::TimeDuration TaskRunnerPosix::GetDelayToNextTaskLocked()
    const {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!immediate_tasks_.empty())
    return TimeDuration(0);
  if (!delayed_tasks_.empty()) {
    return std::max(TimeDuration(0),
                    std::chrono::duration_cast<TimeDuration>(
                        delayed_tasks_.begin()->first - GetTime()));
  }
  return TimeDuration(-1);
}

void TaskRunnerPosix::PostTask(std::function<void()> task) {
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock(lock_);
    was_empty = immediate_tasks_.empty();
    immediate_tasks_.push_back(std::move(task));
  }
  if (was_empty)
    WakeUp();
}

void TaskRunnerPosix::PostDelayedTask(std::function<void()> task,
                                      int delay_ms) {
  PERFETTO_DCHECK(delay_ms >= 0);
  {
    std::lock_guard<std::mutex> lock(lock_);
    delayed_tasks_.insert(std::make_pair(
        GetTime() + std::chrono::milliseconds(delay_ms), std::move(task)));
  }
  WakeUp();
}

void TaskRunnerPosix::AddFileDescriptorWatch(int fd,
                                             std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    pending_poll_tasks_.insert(std::make_pair(fd, std::move(task)));
    poll_tasks_changed_ = true;
  }
  WakeUp();
}

void TaskRunnerPosix::RemoveFileDescriptorWatch(int fd) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    pending_poll_tasks_.insert(std::make_pair(fd, nullptr));
    poll_tasks_changed_ = true;
  }
  // No need to schedule a wake-up for this.
}

}  // namespace base
}  // namespace perfetto
