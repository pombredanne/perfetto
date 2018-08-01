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

#include "src/heapprofd/record_reader.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {

TEST(RecordReaderTest, ZeroLengthRecord) {
  bool called = false;
  auto callback_fn = [&called](size_t size, std::unique_ptr<uint8_t[]>) {
    called = true;
    ASSERT_EQ(size, 0);
  };
  int fd[2];
  ASSERT_NE(pipe(fd), -1);
  RecordReader r(std::move(callback_fn));
  uint64_t size = 0;
  ASSERT_NE(write(fd[1], &size, sizeof(size)), -1);
  while (!called) {
    ssize_t rd = r.Read(fd[0]);
    ASSERT_NE(rd, -1);
    ASSERT_NE(rd, 0);
  }
}

TEST(RecordReaderTest, OneRecord) {
  bool called = false;
  auto callback_fn = [&called](size_t size, std::unique_ptr<uint8_t[]>) {
    called = true;
    ASSERT_EQ(size, 1);
  };
  int fd[2];
  ASSERT_NE(pipe(fd), -1);
  RecordReader r(std::move(callback_fn));
  uint64_t size = 1;
  ASSERT_NE(write(fd[1], &size, sizeof(size)), -1);
  ASSERT_NE(write(fd[1], "1", 1), -1);
  while (!called) {
    ssize_t rd = r.Read(fd[0]);
    ASSERT_NE(rd, -1);
    ASSERT_NE(rd, 0);
  }
}

TEST(RecordReaderTest, TwoRecords) {
  size_t called = 0;
  auto callback_fn = [&called](size_t size, std::unique_ptr<uint8_t[]>) {
    ASSERT_EQ(size, ++called);
  };
  int fd[2];
  ASSERT_NE(pipe(fd), -1);
  RecordReader r(std::move(callback_fn));
  uint64_t size = 1;
  ASSERT_NE(write(fd[1], &size, sizeof(size)), -1);
  ASSERT_NE(write(fd[1], "1", 1), -1);
  size = 2;
  ASSERT_NE(write(fd[1], &size, sizeof(size)), -1);
  ASSERT_NE(write(fd[1], "12", 2), -1);
  while (called != 2) {
    ssize_t rd = r.Read(fd[0]);
    ASSERT_NE(rd, -1);
    ASSERT_NE(rd, 0);
  }
}

}  // namespace perfetto
