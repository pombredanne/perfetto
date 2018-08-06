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

#include "src/profiling/memory/record_reader.h"

#include <sys/socket.h>
#include <sys/types.h>

#include "perfetto/base/scoped_file.h"
#include "src/base/test/test_task_runner.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace {

int ScopedSocketPair(base::ScopedFile scoped_fds[2]) {
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
    return -1;
  scoped_fds[0].reset(fds[0]);
  scoped_fds[1].reset(fds[1]);
  return 0;
}

class RecordReaderListener : public ipc::UnixSocket::EventListener {
 public:
  RecordReaderListener(
      std::function<void(size_t, std::unique_ptr<uint8_t[]>)> fn)
      : reader_(std::move(fn)) {}
  void OnDataAvailable(ipc::UnixSocket* self) { reader_.Read(self); }

 private:
  RecordReader reader_;
};

TEST(RecordReaderTest, ZeroLengthRecord) {
  base::TestTaskRunner task_runner;
  auto callback_called = task_runner.CreateCheckpoint("callback.called");
  auto callback_fn = [&callback_called](size_t size,
                                        std::unique_ptr<uint8_t[]>) {
    ASSERT_EQ(size, 0u);
    callback_called();
  };

  base::ScopedFile fd[2];
  ASSERT_NE(ScopedSocketPair(fd), -1);
  RecordReader r(std::move(callback_fn));
  uint64_t size = 0;
  ASSERT_NE(write(*fd[1], &size, sizeof(size)), -1);

  RecordReaderListener listener(std::move(callback_fn));
  std::unique_ptr<ipc::UnixSocket> recv_socket =
      ipc::UnixSocket::AdoptForTesting(std::move(fd[0]),
                                       ipc::UnixSocket::State::kConnected,
                                       &listener, &task_runner);
  task_runner.RunUntilCheckpoint("callback.called");
}

TEST(RecordReaderTest, OneRecord) {
  base::TestTaskRunner task_runner;
  auto callback_called = task_runner.CreateCheckpoint("callback.called");
  auto callback_fn = [&callback_called](size_t size,
                                        std::unique_ptr<uint8_t[]>) {
    ASSERT_EQ(size, 1u);
    callback_called();
  };

  base::ScopedFile fd[2];
  ASSERT_NE(ScopedSocketPair(fd), -1);
  RecordReader r(std::move(callback_fn));
  uint64_t size = 1;
  ASSERT_NE(write(*fd[1], &size, sizeof(size)), -1);
  ASSERT_NE(write(*fd[1], "1", 1), -1);

  RecordReaderListener listener(std::move(callback_fn));
  std::unique_ptr<ipc::UnixSocket> recv_socket =
      ipc::UnixSocket::AdoptForTesting(std::move(fd[0]),
                                       ipc::UnixSocket::State::kConnected,
                                       &listener, &task_runner);
  task_runner.RunUntilCheckpoint("callback.called");
}

TEST(RecordReaderTest, TwoRecords) {
  base::TestTaskRunner task_runner;
  auto callback_called = task_runner.CreateCheckpoint("callback.called");
  size_t called = 0;
  auto callback_fn = [&callback_called, &called](size_t size,
                                                 std::unique_ptr<uint8_t[]>) {
    ASSERT_EQ(size, 1u);
    if (++called == 2)
      callback_called();
  };

  base::ScopedFile fd[2];
  ASSERT_NE(ScopedSocketPair(fd), -1);
  RecordReader r(std::move(callback_fn));
  uint64_t size = 1;
  ASSERT_NE(write(*fd[1], &size, sizeof(size)), -1);
  ASSERT_NE(write(*fd[1], "1", 1), -1);
  ASSERT_NE(write(*fd[1], &size, sizeof(size)), -1);
  ASSERT_NE(write(*fd[1], "1", 1), -1);

  RecordReaderListener listener(std::move(callback_fn));
  std::unique_ptr<ipc::UnixSocket> recv_socket =
      ipc::UnixSocket::AdoptForTesting(std::move(fd[0]),
                                       ipc::UnixSocket::State::kConnected,
                                       &listener, &task_runner);
  task_runner.RunUntilCheckpoint("callback.called");
}

}  // namespace
}  // namespace perfetto
