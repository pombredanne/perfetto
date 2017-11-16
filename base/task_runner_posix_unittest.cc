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
#include "gtest/gtest.h"

namespace perfetto {
namespace base {
namespace {

TEST(TaskRunnerPosix, RunImmediateTask) {
  TaskRunnerPosix task_runner;
  task_runner.PostTask([&task_runner] { task_runner.Quit(); });
  task_runner.Run();
}

TEST(TaskRunnerPosix, RunDelayedTask) {
  TaskRunnerPosix task_runner;
  task_runner.PostDelayedTask([&task_runner] { task_runner.Quit(); }, 100);
  task_runner.Run();
}

TEST(TaskRunnerPosix, AddAndRemoveFileDescriptorWatch) {
  TaskRunnerPosix task_runner;
  // TODO
}

TEST(TaskRunnerPosix, PostFromOtherThread) {
  TaskRunnerPosix task_runner;
  // TODO
}

}  // namespace
}  // namespace base
}  // namespace perfetto
