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
#include "src/trace_processor/proto_trace_parser.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/trace_sorter.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;

class MockTraceParser : public ProtoTraceParser {
 public:
  MockTraceParser(TraceProcessorContext* context) : ProtoTraceParser(context) {}

  MOCK_METHOD3(ParseFtracePacket,
               void(uint32_t cpu, uint64_t timestamp, const TraceBlobView&));
  MOCK_METHOD1(ParseTracePacket, void(const TraceBlobView&));
};

class TraceSorterTest : public ::testing::Test {
 public:
  TraceSorterTest() {
    context_.sorter.reset(new TraceSorter(&context_, 0 /*window_size*/));
    parser_ = new MockTraceParser(&context_);
    context_.parser.reset(parser_);
    test_buffer_ = std::shared_ptr<uint8_t>(new uint8_t[8],
                                            std::default_delete<uint8_t[]>());
  }

 protected:
  TraceProcessorContext context_;
  MockTraceParser* parser_;
  std::shared_ptr<uint8_t> test_buffer_;
};

TEST_F(TraceSorterTest, TestFtrace) {
  TraceBlobView view(test_buffer_, 0, 1);
  EXPECT_CALL(*parser_, ParseFtracePacket(0, 1000, view));
  context_.sorter->PushFtracePacket(0 /*cpu*/, 1000 /*timestamp*/,
                                    std::move(view));
}

TEST_F(TraceSorterTest, TestTracePacket) {
  TraceBlobView view(test_buffer_, 0, 1);
  EXPECT_CALL(*parser_, ParseTracePacket(view));
  context_.sorter->PushTracePacket(1000, std::move(view));
}

TEST_F(TraceSorterTest, Ordering) {
  TraceBlobView view(test_buffer_, 0, 1);
  TraceBlobView view_length_5(test_buffer_, 0, 5);
  TraceBlobView view_length_2(test_buffer_, 0, 2);

  InSequence s;

  EXPECT_CALL(*parser_, ParseFtracePacket(0, 1000, view));
  EXPECT_CALL(*parser_, ParseTracePacket(view_length_5));
  EXPECT_CALL(*parser_, ParseTracePacket(view_length_2));
  EXPECT_CALL(*parser_, ParseFtracePacket(2, 1200, view));

  context_.sorter->set_window_ns_for_testing(200);
  context_.sorter->PushFtracePacket(2 /*cpu*/, 1200 /*timestamp*/, view);
  context_.sorter->PushTracePacket(1001, view_length_5);
  context_.sorter->PushTracePacket(1100, view_length_2);
  context_.sorter->PushFtracePacket(0 /*cpu*/, 1000 /*timestamp*/, view);

  context_.sorter->MaybeFlushEvents(true);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
