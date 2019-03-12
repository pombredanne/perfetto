/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "perfetto/base/thread_task_runner.h"

#include <condition_variable>
#include <mutex>
#include <thread>

#include "perfetto/base/unix_task_runner.h"

namespace perfetto {
namespace base {

ThreadTaskRunner::ThreadTaskRunner(ThreadTaskRunner&& other) noexcept
    : thread_(std::move(other.thread_)), runner_(other.runner_) {
  other.runner_ = nullptr;
}

ThreadTaskRunner& ThreadTaskRunner::operator=(ThreadTaskRunner&& other) {
  this->~ThreadTaskRunner();
  new (this) ThreadTaskRunner(std::move(other));
  return *this;
}

ThreadTaskRunner::~ThreadTaskRunner() {
  if (runner_)
    runner_->Quit();
  if (thread_.joinable())
    thread_.join();
}

ThreadTaskRunner::ThreadTaskRunner() {
  std::mutex init_lock;
  std::condition_variable init_cv;

  thread_ =
      std::thread(&ThreadTaskRunner::RunTaskThread, this, &init_lock, &init_cv);

  std::unique_lock<std::mutex> lock(init_lock);
  init_cv.wait(lock, [this] { return !!runner_; });
}

void ThreadTaskRunner::RunTaskThread(std::mutex* init_lock,
                                     std::condition_variable* init_cv) {
  UnixTaskRunner task_runner;
  {
    std::lock_guard<std::mutex> lock(*init_lock);
    runner_ = &task_runner;
    // Notify while still holding the lock, as init_cv ceases to exist as soon
    // as the main thread observes a non-null runner_, and it can wake up
    // spuriously (i.e. before the notify if we had unlocked before notifying).
    init_cv->notify_one();
  }
  task_runner.Run();
}

}  // namespace base
}  // namespace perfetto
