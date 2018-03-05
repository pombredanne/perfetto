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

#include "perfetto_cmd.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace {

using base::ScopedFile;

std::pair<ScopedFile, ScopedFile> CreatePipe() {
  int pipe_fds[2];
  PERFETTO_CHECK(pipe(pipe_fds) == 0);
  ScopedFile read_fd(pipe_fds[0]);
  ScopedFile write_fd(pipe_fds[1]);
  return std::pair<ScopedFile, ScopedFile>(std::move(read_fd),
                                           std::move(write_fd));
}

TEST(RateLimiterTest, RoundTripState) {
  auto a_pipe = CreatePipe();
  PerfettoCmdState input;
  PerfettoCmdState output;
  input.set_total_bytes_uploaded(42);
  ASSERT_TRUE(PerfettoCmd::WriteState(*a_pipe.second, input));
  ASSERT_TRUE(PerfettoCmd::ReadState(*a_pipe.first, &output));
  ASSERT_EQ(output.total_bytes_uploaded(), 42u);
}

TEST(RateLimiterTest, LoadFromEmpty) {
  auto a_pipe = CreatePipe();
  PerfettoCmdState output;
  a_pipe.second.reset();
  ASSERT_TRUE(PerfettoCmd::ReadState(*a_pipe.first, &output));
  ASSERT_EQ(output.total_bytes_uploaded(), 0u);
}

}  // namespace
}  // namespace perfetto
