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

class MockTraceParser : public ProtoTraceParser {
 public:
  MockTraceParser(TraceProcessorContext* context) : ProtoTraceParser(context) {}

  MOCK_METHOD5(MOCK_ParseFtracePacket,
               void(uint32_t cpu,
                    uint64_t timestamp,
                    const std::shared_ptr<uint8_t>& buffer,
                    size_t length,
                    const uint8_t* data));

  void ParseFtracePacket(uint32_t cpu,
                         uint64_t timestamp,
                         TraceBlobView tbv) override {
    MOCK_ParseFtracePacket(cpu, timestamp, tbv.buffer(), tbv.length(),
                           tbv.data());
  }

  MOCK_METHOD3(MOCK_ParseTracePacket,
               void(const std::shared_ptr<uint8_t>& buffer,
                    size_t length,
                    const uint8_t* data));

  void ParseTracePacket(TraceBlobView tbv) override {
    MOCK_ParseTracePacket(tbv.buffer(), tbv.length(), tbv.data());
  }
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
  EXPECT_CALL(*parser_, MOCK_ParseFtracePacket(0, 1000, view.buffer(), 1,
                                               view.buffer().get()));
  context_.sorter->PushFtracePacket(0 /*cpu*/, 1000 /*timestamp*/,
                                    std::move(view));
}

TEST_F(TraceSorterTest, TestTracePacket) {
  TraceBlobView view(test_buffer_, 0, 1);
  EXPECT_CALL(*parser_,
              MOCK_ParseTracePacket(view.buffer(), 1, view.buffer().get()));
  context_.sorter->PushTracePacket(1000, std::move(view));
}

TEST_F(TraceSorterTest, Ordering) {
  TraceBlobView view_1(test_buffer_, 0, 1);
  TraceBlobView view_2(test_buffer_, 0, 2);
  TraceBlobView view_3(test_buffer_, 0, 3);
  TraceBlobView view_4(test_buffer_, 0, 4);

  InSequence s;

  EXPECT_CALL(*parser_, MOCK_ParseFtracePacket(0, 1000, view_1.buffer(), 1,
                                               view_1.buffer().get()));
  EXPECT_CALL(*parser_,
              MOCK_ParseTracePacket(view_2.buffer(), 2, view_2.buffer().get()));
  EXPECT_CALL(*parser_,
              MOCK_ParseTracePacket(view_3.buffer(), 3, view_3.buffer().get()));
  EXPECT_CALL(*parser_, MOCK_ParseFtracePacket(2, 1200, view_4.buffer(), 4,
                                               view_4.buffer().get()));

  context_.sorter->set_window_ns_for_testing(200);
  context_.sorter->PushFtracePacket(2 /*cpu*/, 1200 /*timestamp*/,
                                    std::move(view_4));
  context_.sorter->PushTracePacket(1001, std::move(view_2));
  context_.sorter->PushTracePacket(1100, std::move(view_3));
  context_.sorter->PushFtracePacket(0 /*cpu*/, 1000 /*timestamp*/,
                                    std::move(view_1));

  context_.sorter->MaybeFlushEvents(true);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
