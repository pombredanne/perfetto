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

#include "rate_limiter.h"

#include "perfetto/base/scoped_file.h"
#include "perfetto/base/utils.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::StrictMock;
using testing::Invoke;
using testing::Return;

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

class MockRateLimiter : public RateLimiter {
 public:
  MockRateLimiter() {
    ON_CALL(*this, LoadState(_))
        .WillByDefault(Invoke([this](PerfettoCmdState* state) {
          state->set_total_bytes_uploaded(this->input_total_bytes_uploaded);
          state->set_first_trace_timestamp(this->input_start_timestamp);
          state->set_last_trace_timestamp(this->input_end_timestamp);
          return true;
        }));
    ON_CALL(*this, SaveState(_))
        .WillByDefault(Invoke([this](const PerfettoCmdState& state) {
          this->output_total_bytes_uploaded = state.total_bytes_uploaded();
          this->output_start_timestamp = state.first_trace_timestamp();
          this->output_end_timestamp = state.last_trace_timestamp();
          return true;
        }));
  }

  // These are inputs we set for ShouldTrace() to use.
  uint64_t input_total_bytes_uploaded = 0;
  uint64_t input_start_timestamp = 0;
  uint64_t input_end_timestamp = 0;

  // These are outputs we EXPECT_EQ on after ShouldTrace().
  uint64_t output_total_bytes_uploaded = 0;
  uint64_t output_start_timestamp = 0;
  uint64_t output_end_timestamp = 0;

  MOCK_METHOD1(LoadState, bool(PerfettoCmdState*));
  MOCK_METHOD1(SaveState, bool(const PerfettoCmdState&));
};

TEST(RateLimiterTest, RoundTripState) {
  auto a_pipe = CreatePipe();
  PerfettoCmdState input;
  PerfettoCmdState output;
  input.set_total_bytes_uploaded(42);
  ASSERT_TRUE(RateLimiter::WriteState(*a_pipe.second, input));
  ASSERT_TRUE(RateLimiter::ReadState(*a_pipe.first, &output));
  ASSERT_EQ(output.total_bytes_uploaded(), 42u);
}

TEST(RateLimiterTest, LoadFromEmpty) {
  auto a_pipe = CreatePipe();
  a_pipe.second.reset();
  PerfettoCmdState output;
  ASSERT_TRUE(RateLimiter::ReadState(*a_pipe.first, &output));
  ASSERT_EQ(output.total_bytes_uploaded(), 0u);
}

TEST(RateLimiterTest, NotDropBox) {
  StrictMock<MockRateLimiter> limiter;

  ASSERT_TRUE(limiter.ShouldTrace({}));
  ASSERT_TRUE(limiter.TraceDone({}, true, 10000));
}

TEST(RateLimiterTest, NotDropBox_FailedToTrace) {
  StrictMock<MockRateLimiter> limiter;

  ASSERT_TRUE(limiter.ShouldTrace({}));
  ASSERT_FALSE(limiter.TraceDone({}, false, 0));
}

TEST(RateLimiterTest, DropBox_IgnoreGuardrails) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));
  EXPECT_CALL(limiter, SaveState(_));

  args.is_dropbox = true;
  args.ignore_guardrails = true;

  ASSERT_TRUE(limiter.ShouldTrace(args));
  ASSERT_TRUE(limiter.TraceDone(args, true, 1024 * 1024 * 100));
}

TEST(RateLimiterTest, DropBox_EmptyState) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));
  EXPECT_CALL(limiter, SaveState(_));

  args.is_dropbox = true;
  args.current_timestamp = 10000;

  ASSERT_TRUE(limiter.ShouldTrace(args));
  ASSERT_TRUE(limiter.TraceDone(args, true, 1024 * 1024));
  EXPECT_EQ(limiter.output_total_bytes_uploaded, 1024u * 1024u);
  EXPECT_EQ(limiter.output_start_timestamp, 10000u);
  EXPECT_EQ(limiter.output_end_timestamp, 10000u);
}

TEST(RateLimiterTest, DropBox_NormalUpload) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));
  EXPECT_CALL(limiter, SaveState(_));

  args.is_dropbox = true;
  limiter.input_start_timestamp = 10000;
  limiter.input_end_timestamp = limiter.input_start_timestamp + 60 * 10;
  args.current_timestamp = limiter.input_end_timestamp + 60 * 10;
  limiter.input_total_bytes_uploaded = 1024 * 1024 * 2;

  ASSERT_TRUE(limiter.ShouldTrace(args));
  limiter.TraceDone(args, true, 1024 * 1024);
  EXPECT_EQ(limiter.output_total_bytes_uploaded, 1024u * 1024u * 3);
  EXPECT_EQ(limiter.output_start_timestamp, limiter.input_start_timestamp);
  EXPECT_EQ(limiter.output_end_timestamp, args.current_timestamp);
}

TEST(RateLimiterTest, DropBox_FailedToLoadState) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_)).WillOnce(Return(false));
  EXPECT_CALL(limiter, SaveState(_));

  args.is_dropbox = true;

  ASSERT_FALSE(limiter.ShouldTrace(args));

  EXPECT_EQ(limiter.output_total_bytes_uploaded, 0u);
  EXPECT_EQ(limiter.output_start_timestamp, 0u);
  EXPECT_EQ(limiter.output_end_timestamp, 0u);
}

TEST(RateLimiterTest, DropBox_NoTimeTravel) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));
  EXPECT_CALL(limiter, SaveState(_));

  args.is_dropbox = true;
  args.current_timestamp = 99;
  limiter.input_start_timestamp = 100;

  ASSERT_FALSE(limiter.ShouldTrace(args));

  EXPECT_EQ(limiter.output_total_bytes_uploaded, 0u);
  EXPECT_EQ(limiter.output_start_timestamp, 0u);
  EXPECT_EQ(limiter.output_end_timestamp, 0u);
}

TEST(RateLimiterTest, DropBox_TooSoon) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));

  args.is_dropbox = true;
  limiter.input_end_timestamp = 10000;
  args.current_timestamp = 10000 + 60 * 4;

  ASSERT_FALSE(limiter.ShouldTrace(args));
}

TEST(RateLimiterTest, DropBox_TooMuch) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));

  args.is_dropbox = true;
  args.current_timestamp = 60 * 60;
  limiter.input_total_bytes_uploaded = 10 * 1024 * 1024 + 1;

  ASSERT_FALSE(limiter.ShouldTrace(args));
}

TEST(RateLimiterTest, DropBox_TooMuchWasUploaded) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));
  EXPECT_CALL(limiter, SaveState(_));

  args.is_dropbox = true;
  limiter.input_start_timestamp = 1;
  limiter.input_end_timestamp = 1;
  args.current_timestamp = 60 * 60 * 24 + 2;
  limiter.input_total_bytes_uploaded = 10 * 1024 * 1024 + 1;

  ASSERT_TRUE(limiter.ShouldTrace(args));
  ASSERT_TRUE(limiter.TraceDone(args, true, 1024 * 1024));

  EXPECT_EQ(limiter.output_total_bytes_uploaded, 1024u * 1024u);
  EXPECT_EQ(limiter.output_start_timestamp, args.current_timestamp);
  EXPECT_EQ(limiter.output_end_timestamp, args.current_timestamp);
}

TEST(RateLimiterTest, DropBox_FailedToUpload) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));

  args.is_dropbox = true;
  args.current_timestamp = 10000;

  ASSERT_TRUE(limiter.ShouldTrace(args));
  ASSERT_FALSE(limiter.TraceDone(args, false, 1024 * 1024));
}

TEST(RateLimiterTest, DropBox_FailedToSave) {
  StrictMock<MockRateLimiter> limiter;
  RateLimiter::Args args;

  EXPECT_CALL(limiter, LoadState(_));
  EXPECT_CALL(limiter, SaveState(_)).WillOnce(Return(false));

  args.is_dropbox = true;
  args.current_timestamp = 10000;

  ASSERT_TRUE(limiter.ShouldTrace(args));
  ASSERT_FALSE(limiter.TraceDone(args, true, 1024 * 1024));
}

}  // namespace

}  // namespace perfetto
