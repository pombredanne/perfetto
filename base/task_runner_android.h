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

#ifndef PERFETTO_BASE_TASK_RUNNER_ANDROID_H_
#define PERFETTO_BASE_TASK_RUNNER_ANDROID_H_

#include "base/scoped_file.h"
#include "base/task_runner.h"
#include "base/thread_checker.h"

#include <poll.h>
#include <chrono>
#include <map>
#include <mutex>
#include <queue>

#include <android/looper.h>

namespace perfetto {
namespace base {

// Runs a task runner on a thread owned by an Android Looper (ALooper).
class TaskRunnerAndroid : public TaskRunner {
 public:
  explicit TaskRunnerAndroid(ALooper* looper);
  ~TaskRunnerAndroid() override;

  // TODO FIXME We can't own the thread on Android -- we need a way to talk to
  // android.os.Handler instead.
  //
  // Some thoughts:
  // - Chrome can only watch for fds on an IO thread.
  // - Does Java's Handler support watching fd events? Or do we need to talk
  //   both Handler and ALooper?
  void Run();
  void Quit();

  // TaskRunner implementation:
  void PostTask(std::function<void()>) override;
  void PostDelayedTask(std::function<void()>, int delay_ms) override;
  void AddFileDescriptorWatch(int fd, std::function<void()>) override;
  void RemoveFileDescriptorWatch(int fd) override;

 private:
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
  using TimeDuration = std::chrono::milliseconds;
  TimePoint GetTime() const;

  void UpdatePollTasksLocked();

  void WakeUp();

  TimeDuration GetDelayToNextTaskLocked() const;
  void RunImmediateTask();
  void RunDelayedTask();
  void RunFileDescriptorWatches();

  ThreadChecker thread_checker_;

  // Active set of fds we are watching.
  std::map<int, std::function<void()>> poll_tasks_;

  ALooper* looper_;

  // --- Begin lock-protected members.

  std::mutex lock_;

  std::deque<std::function<void()>> immediate_tasks_;
  std::multimap<TimePoint, std::function<void()>> delayed_tasks_;
  bool done_ = false;

  // A non-null function indicates a newly added watch, a null function a
  // removed watch.
  std::map<int, std::function<void()>> pending_poll_tasks_;
  bool poll_tasks_changed_ = true;

  // --- End lock-protected members.
};

}  // namespace base
}  // namespace perfetto

#endif  // PERFETTO_BASE_TASK_RUNNER_ANDROID_H_
