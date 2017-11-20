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

  // Register a dummy fd watch which is used to wake up this thread from other
  // threads.
  AddFileDescriptorWatch(control_read_.get(), [this] {
    char dummy;
    if (read(control_read_.get(), &dummy, 1) <= 0)
      PERFETTO_DPLOG("read()");
  });
}

TaskRunnerPosix::~TaskRunnerPosix() = default;

TaskRunnerPosix::TimePoint TaskRunnerPosix::GetTime() const {
  return std::chrono::steady_clock::now();
}

void TaskRunnerPosix::WakeUp() {
  // If we're running on the main thread there's no need to schedule a wake-up
  // since we're already inside Run().
  if (thread_checker_.CalledOnValidThread())
    return;
  const char dummy = 'P';
  if (write(control_write_.get(), &dummy, 1) <= 0)
    PERFETTO_DPLOG("write()");
}

void TaskRunnerPosix::Run() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  while (true) {
    switch (WaitForEvent()) {
      case Event::kQuit:
        return;
      case Event::kTaskRunnable:
        // To avoid starvation we interleave immediate and delayed task
        // execution.
        RunImmediateAndDelayedTask();
        break;
      case Event::kFileDescriptorReadable:
        PostFileDescriptorWatches();
        break;
    }
  }
}

TaskRunnerPosix::Event TaskRunnerPosix::WaitForEvent() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  int poll_timeout;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (done_)
      return Event::kQuit;
    poll_timeout = static_cast<int>(GetDelayToNextTaskLocked().count());
    // Don't start polling until we run out of runnable tasks (immediate or ones
    // with expired delays).
    if (!poll_timeout)
      return Event::kTaskRunnable;
    UpdateWatchTasksLocked();
  }
  int ret = PERFETTO_EINTR(
      poll(&poll_fds_[0], static_cast<nfds_t>(poll_fds_.size()), poll_timeout));
  if (ret == -1) {
    PERFETTO_DPLOG("poll()");
    return Event::kQuit;
  }
  bool did_timeout = (ret == 0);
  return did_timeout ? Event::kTaskRunnable : Event::kFileDescriptorReadable;
}

void TaskRunnerPosix::Quit() {
  {
    std::lock_guard<std::mutex> lock(lock_);
    done_ = true;
  }
  WakeUp();
}

bool TaskRunnerPosix::UpdateWatchTasksLocked() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!watch_tasks_changed_)
    return false;
  watch_tasks_changed_ = false;
  poll_fds_.clear();
  for (const auto& it : watch_tasks_)
    poll_fds_.push_back({it.first, POLLIN, 0});
  return true;
}

void TaskRunnerPosix::RunImmediateAndDelayedTask() {
  // TODO(skyostil): Add a separate work queue in case in case locking overhead
  // becomes an issue.
  std::function<void()> immediate_task;
  std::function<void()> delayed_task;
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!immediate_tasks_.empty()) {
      immediate_task = std::move(immediate_tasks_.front());
      immediate_tasks_.pop_front();
    }
    if (!delayed_tasks_.empty()) {
      auto it = delayed_tasks_.begin();
      if (GetTime() >= it->first) {
        delayed_task = std::move(it->second);
        delayed_tasks_.erase(it);
      }
    }
  }
  if (immediate_task)
    immediate_task();
  if (delayed_task)
    delayed_task();
}

void TaskRunnerPosix::PostFileDescriptorWatches() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (size_t i = 0; i < poll_fds_.size(); i++) {
    if (!(poll_fds_[i].revents & POLLIN))
      continue;
    poll_fds_[i].revents = 0;
    int fd = poll_fds_[i].fd;
    PostTask([this, fd] {
      std::function<void()> task;
      {
        std::lock_guard<std::mutex> lock(lock_);
        auto it = watch_tasks_.find(fd);
        if (it == watch_tasks_.end())
          return;
        task = it->second;
      }
      task();
    });
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
    watch_tasks_[fd] = std::move(task);
    watch_tasks_changed_ = true;
  }
  WakeUp();
}

void TaskRunnerPosix::RemoveFileDescriptorWatch(int fd) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    watch_tasks_.erase(fd);
    watch_tasks_changed_ = true;
  }
  // No need to schedule a wake-up for this.
}

}  // namespace base
}  // namespace perfetto
