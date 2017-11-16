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

#include "base/task_runner_android.h"

#include <poll.h>
#include <unistd.h>

namespace perfetto {
namespace base {

TaskRunnerAndroid::TaskRunnerAndroid(ALooper* looper) : looper_(looper) {}

TaskRunnerAndroid::~TaskRunnerAndroid() = default;

TaskRunnerAndroid::TimePoint TaskRunnerAndroid::GetTime() const {
  return std::chrono::steady_clock::now();
}

void TaskRunnerAndroid::WakeUp() {
  // If we're running on the main thread there's no need to schedule a wake-up.
  if (thread_checker_.CalledOnValidThread())
    return;
  ALooper_wake(looper_);
}

void TaskRunnerAndroid::Run() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  while (true) {
    int next_task_delay_ms;
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (done_)
        break;
      next_task_delay_ms = static_cast<int>(GetDelayToNextTaskLocked().count());
    }
    int ret = 0;
    // Don't start polling until we run out of runnable tasks.
    if (next_task_delay_ms) {
      int unused_fd;
      int unused_events;
      void* unused_data;
      ret = ALooper_pollAll(next_task_delay_ms, &unused_fd, &unused_events,
                            &unused_data);
    }
    if (ret == ALOOPER_POLL_ERROR) {
      PERFETTO_DPLOG("ALooper_pollAll()");
      return;
    }
    RunImmediateTask();
    RunDelayedTask();
  }
}

void TaskRunnerAndroid::Quit() {
  {
    std::lock_guard<std::mutex> lock(lock_);
    done_ = true;
  }
  WakeUp();
}

void TaskRunnerAndroid::UpdatePollTasksLocked() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!poll_tasks_changed_)
    return;
  poll_tasks_changed_ = false;

  // Add or remove fds.
  for (const auto& it : pending_poll_tasks_) {
    if (!it.second) {
      poll_tasks_.erase(it.first);
    } else {
      poll_tasks_.insert(std::make_pair(it.first, std::move(it.second)));
    }
  }
}

void TaskRunnerAndroid::RunImmediateTask() {
  if (immediate_tasks_.empty())
    return;
  auto task = std::move(immediate_tasks_.front());
  immediate_tasks_.pop_front();
  task();
}

void TaskRunnerAndroid::RunDelayedTask() {
  if (delayed_tasks_.empty())
    return;
  auto it = delayed_tasks_.begin();
  if (GetTime() < it->first)
    return;
  auto task = std::move(it->second);
  delayed_tasks_.erase(it);
  task();
}

TaskRunnerAndroid::TimeDuration TaskRunnerAndroid::GetDelayToNextTaskLocked()
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

void TaskRunnerAndroid::PostTask(std::function<void()> task) {
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock(lock_);
    was_empty = immediate_tasks_.empty();
    immediate_tasks_.push_back(std::move(task));
  }
  if (was_empty)
    WakeUp();
}

void TaskRunnerAndroid::PostDelayedTask(std::function<void()> task,
                                        int delay_ms) {
  PERFETTO_DCHECK(delay_ms >= 0);
  {
    std::lock_guard<std::mutex> lock(lock_);
    delayed_tasks_.insert(std::make_pair(
        GetTime() + std::chrono::milliseconds(delay_ms), std::move(task)));
  }
  WakeUp();
}

void TaskRunnerAndroid::AddFileDescriptorWatch(int fd,
                                               std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    pending_poll_tasks_.insert(std::make_pair(fd, std::move(task)));
    poll_tasks_changed_ = true;
  }
  ALooper_addFd(looper_, fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                [](int signalled_fd, int events, void* data) -> int {
                  TaskRunnerAndroid* task_runner =
                      reinterpret_cast<TaskRunnerAndroid*>(data);
                  PERFETTO_DCHECK_THREAD(task_runner->thread_checker_);
                  if (!(events & ALOOPER_EVENT_INPUT))
                    return 1;
                  {
                    std::lock_guard<std::mutex> lock(task_runner->lock_);
                    task_runner->UpdatePollTasksLocked();
                  }
                  const auto& it = task_runner->poll_tasks_.find(signalled_fd);
                  if (it == task_runner->poll_tasks_.end() || !it->second)
                    return 0;
                  it->second();
                  return 1;
                },
                this);
}

void TaskRunnerAndroid::RemoveFileDescriptorWatch(int fd) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    pending_poll_tasks_.insert(std::make_pair(fd, nullptr));
    poll_tasks_changed_ = true;
  }
  ALooper_removeFd(looper_, fd);
}

}  // namespace base
}  // namespace perfetto
