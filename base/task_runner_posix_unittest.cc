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

#include "base/scoped_file.h"
#include "gtest/gtest.h"

#include <thread>

namespace perfetto {
namespace base {
namespace {

struct Pipe {
  Pipe() {
    int pipe_fds[2];
    PERFETTO_DCHECK(pipe(pipe_fds) == 0);
    read_fd.reset(pipe_fds[0]);
    write_fd.reset(pipe_fds[1]);
    // Make the pipe initially readable.
    Write();
  }

  void Write() {
    const char b = '?';
    PERFETTO_DCHECK(write(write_fd.get(), &b, 1) == 1);
  }

  ScopedFile read_fd;
  ScopedFile write_fd;
};

TEST(TaskRunnerPosix, PostImmediateTask) {
  TaskRunnerPosix task_runner;
  int counter = 0;
  task_runner.PostTask([&counter] { counter = (counter << 4) | 1; });
  task_runner.PostTask([&counter] { counter = (counter << 4) | 2; });
  task_runner.PostTask([&counter] { counter = (counter << 4) | 3; });
  task_runner.PostTask([&counter] { counter = (counter << 4) | 4; });
  task_runner.PostTask([&task_runner] { task_runner.Quit(); });
  task_runner.Run();
  EXPECT_EQ(0x1234, counter);
}

TEST(TaskRunnerPosix, PostDelayedTask) {
  TaskRunnerPosix task_runner;
  int counter = 0;
  task_runner.PostDelayedTask([&counter] { counter = (counter << 4) | 1; }, 5);
  task_runner.PostDelayedTask([&counter] { counter = (counter << 4) | 2; }, 10);
  task_runner.PostDelayedTask([&counter] { counter = (counter << 4) | 3; }, 15);
  task_runner.PostDelayedTask([&counter] { counter = (counter << 4) | 4; }, 15);
  task_runner.PostDelayedTask([&task_runner] { task_runner.Quit(); }, 20);
  task_runner.Run();
  EXPECT_EQ(0x1234, counter);
}

TEST(TaskRunnerPosix, PostImmediateTaskFromTask) {
  TaskRunnerPosix task_runner;
  task_runner.PostTask([&task_runner] {
    task_runner.PostTask([&task_runner] { task_runner.Quit(); });
  });
  task_runner.Run();
}

TEST(TaskRunnerPosix, PostDelayedTaskFromTask) {
  TaskRunnerPosix task_runner;
  task_runner.PostTask([&task_runner] {
    task_runner.PostDelayedTask([&task_runner] { task_runner.Quit(); }, 10);
  });
  task_runner.Run();
}

TEST(TaskRunnerPosix, PostImmediateTaskFromOtherThread) {
  TaskRunnerPosix task_runner;
  ThreadChecker thread_checker;
  int counter = 0;
  std::thread thread([&task_runner, &counter, &thread_checker] {
    task_runner.PostTask([&thread_checker] {
      EXPECT_TRUE(thread_checker.CalledOnValidThread());
    });
    task_runner.PostTask([&counter] { counter = (counter << 4) | 1; });
    task_runner.PostTask([&counter] { counter = (counter << 4) | 2; });
    task_runner.PostTask([&counter] { counter = (counter << 4) | 3; });
    task_runner.PostTask([&counter] { counter = (counter << 4) | 4; });
    task_runner.PostTask([&task_runner] { task_runner.Quit(); });
  });
  task_runner.Run();
  thread.join();
  EXPECT_EQ(0x1234, counter);
}

TEST(TaskRunnerPosix, PostDelayedTaskFromOtherThread) {
  TaskRunnerPosix task_runner;
  std::thread thread([&task_runner] {
    task_runner.PostDelayedTask([&task_runner] { task_runner.Quit(); }, 10);
  });
  task_runner.Run();
  thread.join();
}

TEST(TaskRunnerPosix, AddFileDescriptorWatch) {
  TaskRunnerPosix task_runner;
  Pipe pipe;
  task_runner.AddFileDescriptorWatch(pipe.read_fd.get(),
                                     [&task_runner] { task_runner.Quit(); });
  task_runner.Run();
}

TEST(TaskRunnerPosix, RemoveFileDescriptorWatch) {
  TaskRunnerPosix task_runner;
  Pipe pipe;

  bool watch_ran = false;
  task_runner.AddFileDescriptorWatch(pipe.read_fd.get(),
                                     [&watch_ran] { watch_ran = true; });
  task_runner.RemoveFileDescriptorWatch(pipe.read_fd.get());
  task_runner.PostDelayedTask([&task_runner] { task_runner.Quit(); }, 10);
  task_runner.Run();

  EXPECT_FALSE(watch_ran);
}

TEST(TaskRunnerPosix, RemoveFileDescriptorWatchFromTask) {
  TaskRunnerPosix task_runner;
  Pipe pipe;

  bool watch_ran = false;
  task_runner.AddFileDescriptorWatch(pipe.read_fd.get(),
                                     [&watch_ran] { watch_ran = true; });
  task_runner.PostTask([&task_runner, &pipe] {
    task_runner.RemoveFileDescriptorWatch(pipe.read_fd.get());
  });
  task_runner.PostDelayedTask([&task_runner] { task_runner.Quit(); }, 10);
  task_runner.Run();

  EXPECT_FALSE(watch_ran);
}

TEST(TaskRunnerPosix, AddFileDescriptorWatchFromAnotherWatch) {
  TaskRunnerPosix task_runner;
  Pipe pipe;
  Pipe pipe2;

  task_runner.AddFileDescriptorWatch(
      pipe.read_fd.get(), [&task_runner, &pipe2] {
        task_runner.AddFileDescriptorWatch(
            pipe2.read_fd.get(), [&task_runner] { task_runner.Quit(); });
      });
  task_runner.Run();
}

TEST(TaskRunnerPosix, RemoveFileDescriptorWatchFromAnotherWatch) {
  TaskRunnerPosix task_runner;
  Pipe pipe;
  Pipe pipe2;

  bool watch_ran = false;
  task_runner.AddFileDescriptorWatch(
      pipe.read_fd.get(), [&task_runner, &pipe2] {
        task_runner.RemoveFileDescriptorWatch(pipe2.read_fd.get());
      });
  task_runner.AddFileDescriptorWatch(pipe2.read_fd.get(),
                                     [&watch_ran] { watch_ran = true; });
  task_runner.PostDelayedTask([&task_runner] { task_runner.Quit(); }, 10);
  task_runner.Run();

  EXPECT_FALSE(watch_ran);
}

TEST(TaskRunnerPosix, ReplaceFileDescriptorWatchFromAnotherWatch) {
  TaskRunnerPosix task_runner;
  Pipe pipe;
  Pipe pipe2;

  bool watch_ran = false;
  task_runner.AddFileDescriptorWatch(
      pipe.read_fd.get(), [&task_runner, &pipe2] {
        task_runner.RemoveFileDescriptorWatch(pipe2.read_fd.get());
        task_runner.AddFileDescriptorWatch(
            pipe2.read_fd.get(), [&task_runner] { task_runner.Quit(); });
      });
  task_runner.AddFileDescriptorWatch(pipe2.read_fd.get(),
                                     [&watch_ran] { watch_ran = true; });
  task_runner.Run();

  EXPECT_FALSE(watch_ran);
}

TEST(TaskRunnerPosix, AddFileDescriptorWatchFromAnotherThread) {
  TaskRunnerPosix task_runner;
  Pipe pipe;

  std::thread thread([&task_runner, &pipe] {
    task_runner.AddFileDescriptorWatch(pipe.read_fd.get(),
                                       [&task_runner] { task_runner.Quit(); });
  });
  task_runner.Run();
  thread.join();
}

TEST(TaskRunnerPosix, FileDescriptorWatchWithMultipleEvents) {
  TaskRunnerPosix task_runner;
  Pipe pipe;

  int event_count = 0;
  task_runner.AddFileDescriptorWatch(
      pipe.read_fd.get(), [&task_runner, &pipe, &event_count] {
        if (++event_count == 3) {
          task_runner.Quit();
          return;
        }
        char b;
        ASSERT_EQ(1, read(pipe.read_fd.get(), &b, 1));
      });
  task_runner.PostTask([&pipe] { pipe.Write(); });
  task_runner.PostTask([&pipe] { pipe.Write(); });
  task_runner.Run();
}

}  // namespace
}  // namespace base
}  // namespace perfetto
