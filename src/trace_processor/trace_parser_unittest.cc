/*
 * Copyright (C) 2017 The Android Open foo Project
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

#include "src/trace_processor/trace_parser.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "perfetto/base/logging.h"
#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::testing::Eq;
using ::testing::Pointwise;

class FakeStringBlobReader : public BlobReader {
 public:
  FakeStringBlobReader(const std::string& data) : data_(data) {}
  ~FakeStringBlobReader() override {}

  uint32_t Read(uint64_t offset, uint32_t len, uint8_t* dst) override {
    PERFETTO_CHECK(offset <= data_.size());
    uint32_t read = std::min(static_cast<uint32_t>(data_.size() - offset), len);
    memcpy(dst, data_.c_str() + offset, read);
    return read;
  }

 private:
  std::string data_;
};

class MockTraceStorageInserter : public TraceStorageInserter {
 public:
  MockTraceStorageInserter() : TraceStorageInserter(nullptr) {}

  MOCK_METHOD7(InsertSchedSwitch,
               void(uint32_t cpu,
                    uint64_t timestamp,
                    uint32_t prev_pid,
                    uint32_t prev_state,
                    const char* prev_comm,
                    size_t prev_comm_len,
                    uint32_t next_pid));
};

TEST(TraceParser, LoadSinglePacket) {
  protos::Trace trace;

  auto* bundle = trace.add_packet()->mutable_ftrace_events();
  bundle->set_cpu(10);

  auto* event = bundle->add_event();
  event->set_timestamp(1000);

  static const char kProcName[] = "proc1";
  auto* sched_switch = event->mutable_sched_switch();
  sched_switch->set_prev_pid(10);
  sched_switch->set_prev_state(32);
  sched_switch->set_prev_comm(kProcName);
  sched_switch->set_next_pid(100);

  MockTraceStorageInserter inserter;
  EXPECT_CALL(inserter,
              InsertSchedSwitch(10, 1000, 10, 32, Pointwise(Eq(), kProcName),
                                sizeof(kProcName) - 1, 100))
      .Times(1);

  FakeStringBlobReader reader(trace.SerializeAsString());
  TraceParser parser(&reader, &inserter, 1024);
  parser.ParseNextChunk();
}

TEST(TraceParser, LoadMultiplePacket) {
  // TODO(lalitm): write this test.
}

TEST(TraceParser, RepeatedLoadSinglePacket) {
  // TODO(lalitm): write this test.
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
