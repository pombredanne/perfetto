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

using testing::_;
using testing::StrictMock;
using testing::Invoke;
using testing::Return;

namespace perfetto {

namespace {

class DelegateMock : public PerfettoCmdLogic::Delegate {
 public:
  DelegateMock() {
    ON_CALL(*this, DoTrace(_))
        .WillByDefault(Invoke([this](uint64_t* bytes_uploaded_out = nullptr) {
          if (bytes_uploaded_out)
            *bytes_uploaded_out = this->input_bytes_uploaded;
          return true;
        }));
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

  // These are inputs we set for Run() to use.
  uint64_t input_bytes_uploaded = 0;
  uint64_t input_total_bytes_uploaded = 0;
  uint64_t input_start_timestamp = 0;
  uint64_t input_end_timestamp = 0;

  // These are outputs we EXPECT_EQ on after Run().
  uint64_t output_total_bytes_uploaded = 0;
  uint64_t output_start_timestamp = 0;
  uint64_t output_end_timestamp = 0;

  MOCK_METHOD1(LoadState, bool(PerfettoCmdState*));
  MOCK_METHOD1(SaveState, bool(const PerfettoCmdState&));
  MOCK_METHOD1(DoTrace, bool(uint64_t* uploaded_bytes));
};

using base::ScopedFile;

std::pair<ScopedFile, ScopedFile> CreatePipe() {
  int pipe_fds[2];
  PERFETTO_CHECK(pipe(pipe_fds) == 0);
  ScopedFile read_fd(pipe_fds[0]);
  ScopedFile write_fd(pipe_fds[1]);
  return std::pair<ScopedFile, ScopedFile>(std::move(read_fd),
                                           std::move(write_fd));
}

TEST(PerfettoCmdTest, RoundTripState) {
  auto a_pipe = CreatePipe();
  PerfettoCmdState input;
  PerfettoCmdState output;
  input.set_total_bytes_uploaded(42);
  ASSERT_TRUE(PerfettoCmd::WriteState(*a_pipe.second, input));
  ASSERT_TRUE(PerfettoCmd::ReadState(*a_pipe.first, &output));
  ASSERT_EQ(output.total_bytes_uploaded(), 42u);
}

TEST(PerfettoCmdTest, LoadFromEmpty) {
  auto a_pipe = CreatePipe();
  PerfettoCmdState output;
  a_pipe.second.reset();
  ASSERT_TRUE(PerfettoCmd::ReadState(*a_pipe.first, &output));
  ASSERT_EQ(output.total_bytes_uploaded(), 0u);
}

TEST(PerfettoCmdTest, NotDropBox) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);

  EXPECT_CALL(delegate, DoTrace(_));
  EXPECT_EQ(logic.Run({}), 0);
}

TEST(PerfettoCmdTest, NotDropBox_FailedToTrace) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);

  EXPECT_CALL(delegate, DoTrace(_)).WillOnce(Return(false));
  EXPECT_EQ(logic.Run({}), 1);
}

TEST(PerfettoCmdTest, DropBox_IgnoreGuardrails) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));
  EXPECT_CALL(delegate, DoTrace(_));
  EXPECT_CALL(delegate, SaveState(_));

  delegate.input_total_bytes_uploaded = 1024 * 1024 * 100;
  args.is_dropbox = true;
  args.ignore_guardrails = true;

  ASSERT_EQ(logic.Run(args), 0);
}

TEST(PerfettoCmdTest, DropBox_EmptyState) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));
  EXPECT_CALL(delegate, DoTrace(_));
  EXPECT_CALL(delegate, SaveState(_));

  args.is_dropbox = true;
  args.current_timestamp = 10000;
  delegate.input_bytes_uploaded = 1024 * 1024;

  ASSERT_EQ(logic.Run(args), 0);
  EXPECT_EQ(delegate.output_total_bytes_uploaded, 1024u * 1024u);
  EXPECT_EQ(delegate.output_start_timestamp, 10000u);
  EXPECT_EQ(delegate.output_end_timestamp, 10000u);
}

TEST(PerfettoCmdTest, DropBox_NormalUpload) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));
  EXPECT_CALL(delegate, DoTrace(_));
  EXPECT_CALL(delegate, SaveState(_));

  args.is_dropbox = true;
  delegate.input_start_timestamp = 10000;
  delegate.input_end_timestamp = delegate.input_start_timestamp + 60 * 10;
  args.current_timestamp = delegate.input_end_timestamp + 60 * 10;
  delegate.input_total_bytes_uploaded = 1024 * 1024 * 2;
  delegate.input_bytes_uploaded = 1024 * 1024;

  ASSERT_EQ(logic.Run(args), 0);
  EXPECT_EQ(delegate.output_total_bytes_uploaded, 1024u * 1024u * 3);
  EXPECT_EQ(delegate.output_start_timestamp, delegate.input_start_timestamp);
  EXPECT_EQ(delegate.output_end_timestamp, args.current_timestamp);
}

TEST(PerfettoCmdTest, DropBox_FailedToLoadState) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_)).WillOnce(Return(false));
  EXPECT_CALL(delegate, SaveState(_));

  args.is_dropbox = true;

  ASSERT_EQ(logic.Run(args), 1);
  EXPECT_EQ(delegate.output_total_bytes_uploaded, 0u);
  EXPECT_EQ(delegate.output_start_timestamp, 0u);
  EXPECT_EQ(delegate.output_end_timestamp, 0u);
}

TEST(PerfettoCmdTest, DropBox_NoTimeTravel) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));
  EXPECT_CALL(delegate, SaveState(_));

  args.is_dropbox = true;
  args.current_timestamp = 99;
  delegate.input_start_timestamp = 100;

  ASSERT_EQ(logic.Run(args), 1);
  EXPECT_EQ(delegate.output_total_bytes_uploaded, 0u);
  EXPECT_EQ(delegate.output_start_timestamp, 0u);
  EXPECT_EQ(delegate.output_end_timestamp, 0u);
}

TEST(PerfettoCmdTest, DropBox_TooSoon) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));

  args.is_dropbox = true;
  delegate.input_end_timestamp = 10000;
  args.current_timestamp = 10000 + 60 * 4;

  ASSERT_EQ(logic.Run(args), 1);
}

TEST(PerfettoCmdTest, DropBox_TooMuch) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));

  args.is_dropbox = true;
  args.current_timestamp = 60 * 60;
  delegate.input_total_bytes_uploaded = 10 * 1024 * 1024 + 1;

  ASSERT_EQ(logic.Run(args), 1);
}

TEST(PerfettoCmdTest, DropBox_TooMuchWasUploaded) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));
  EXPECT_CALL(delegate, DoTrace(_));
  EXPECT_CALL(delegate, SaveState(_));

  args.is_dropbox = true;
  delegate.input_start_timestamp = 1;
  delegate.input_end_timestamp = 1;
  args.current_timestamp = 60 * 60 * 24 + 2;
  delegate.input_total_bytes_uploaded = 10 * 1024 * 1024 + 1;
  delegate.input_bytes_uploaded = 1024 * 1024;

  ASSERT_EQ(logic.Run(args), 0);
  EXPECT_EQ(delegate.output_total_bytes_uploaded, 1024u * 1024u);
  EXPECT_EQ(delegate.output_start_timestamp, args.current_timestamp);
  EXPECT_EQ(delegate.output_end_timestamp, args.current_timestamp);
}

TEST(PerfettoCmdTest, DropBox_FailedToUpload) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));
  EXPECT_CALL(delegate, DoTrace(_)).WillOnce(Return(false));

  args.is_dropbox = true;
  args.current_timestamp = 10000;
  delegate.input_bytes_uploaded = 1024 * 1024;

  ASSERT_EQ(logic.Run(args), 1);
}

TEST(PerfettoCmdTest, DropBox_FailedToSave) {
  StrictMock<DelegateMock> delegate;
  PerfettoCmdLogic logic(&delegate);
  PerfettoCmdLogic::Args args;

  EXPECT_CALL(delegate, LoadState(_));
  EXPECT_CALL(delegate, DoTrace(_));
  EXPECT_CALL(delegate, SaveState(_)).WillOnce(Return(false));

  args.is_dropbox = true;
  args.current_timestamp = 10000;
  delegate.input_bytes_uploaded = 1024 * 1024;

  ASSERT_EQ(logic.Run(args), 1);
}

}  // namespace

}  // namespace perfetto
