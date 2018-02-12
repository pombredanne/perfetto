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

#include "src/tracing/core/packet_stream_validator.h"

#include <string>

#include "gtest/gtest.h"

#include "perfetto/trace/trace_packet.pb.h"

namespace perfetto {
namespace {

TEST(PacketStreamValidatorTest, NullPacket) {
  std::string ser_buf;
  ChunkSequence seq;
  PacketStreamValidator validator(&seq);
  EXPECT_TRUE(validator.Validate());
}

TEST(PacketStreamValidatorTest, SimplePacket) {
  protos::TracePacket proto;
  proto.mutable_for_testing()->set_str("string field");
  std::string ser_buf = proto.SerializeAsString();

  ChunkSequence seq;
  seq.emplace_back(&ser_buf[0], ser_buf.size());
  PacketStreamValidator validator(&seq);
  EXPECT_TRUE(validator.Validate());
}

TEST(PacketStreamValidatorTest, ComplexPacket) {
  protos::TracePacket proto;
  proto.mutable_for_testing()->set_str("string field");
  proto.mutable_ftrace_events()->set_cpu(0);
  auto* ft = proto.mutable_ftrace_events()->add_event();
  ft->set_pid(42);
  ft->mutable_sched_switch()->set_prev_comm("tom");
  ft->mutable_sched_switch()->set_prev_pid(123);
  ft->mutable_sched_switch()->set_next_comm("jerry");
  ft->mutable_sched_switch()->set_next_pid(456);
  std::string ser_buf = proto.SerializeAsString();

  ChunkSequence seq;
  seq.emplace_back(&ser_buf[0], ser_buf.size());
  PacketStreamValidator validator(&seq);
  EXPECT_TRUE(validator.Validate());
}

TEST(PacketStreamValidatorTest, SimplePacketWithUid) {
  protos::TracePacket proto;
  proto.set_trusted_uid(123);
  std::string ser_buf = proto.SerializeAsString();

  ChunkSequence seq;
  seq.emplace_back(&ser_buf[0], ser_buf.size());
  PacketStreamValidator validator(&seq);
  EXPECT_FALSE(validator.Validate());
}

TEST(PacketStreamValidatorTest, ComplexPacketWithUid) {
  protos::TracePacket proto;
  proto.mutable_for_testing()->set_str("string field");
  proto.mutable_ftrace_events()->set_cpu(0);
  auto* ft = proto.mutable_ftrace_events()->add_event();
  ft->set_pid(42);
  ft->mutable_sched_switch()->set_prev_comm("tom");
  ft->mutable_sched_switch()->set_prev_pid(123);
  ft->mutable_sched_switch()->set_next_comm("jerry");
  ft->mutable_sched_switch()->set_next_pid(456);
  proto.set_trusted_uid(123);
  std::string ser_buf = proto.SerializeAsString();

  ChunkSequence seq;
  seq.emplace_back(&ser_buf[0], ser_buf.size());
  PacketStreamValidator validator(&seq);
  EXPECT_FALSE(validator.Validate());
}

TEST(PacketStreamValidatorTest, FragmentedPacket) {
  protos::TracePacket proto;
  proto.mutable_for_testing()->set_str("string field");
  proto.mutable_ftrace_events()->set_cpu(0);
  auto* ft = proto.mutable_ftrace_events()->add_event();
  ft->set_pid(42);
  ft->mutable_sched_switch()->set_prev_comm("tom");
  ft->mutable_sched_switch()->set_prev_pid(123);
  ft->mutable_sched_switch()->set_next_comm("jerry");
  ft->mutable_sched_switch()->set_next_pid(456);
  std::string ser_buf = proto.SerializeAsString();

  for (size_t i = 0; i < ser_buf.size(); i++) {
    ChunkSequence seq;
    seq.emplace_back(&ser_buf[0], i);
    seq.emplace_back(&ser_buf[i], ser_buf.size() - i);
    PacketStreamValidator validator(&seq);
    EXPECT_TRUE(validator.Validate());
  }
}

TEST(PacketStreamValidatorTest, FragmentedPacketWithUid) {
  protos::TracePacket proto;
  proto.mutable_for_testing()->set_str("string field");
  proto.set_trusted_uid(123);
  proto.mutable_ftrace_events()->set_cpu(0);
  auto* ft = proto.mutable_ftrace_events()->add_event();
  ft->set_pid(42);
  ft->mutable_sched_switch()->set_prev_comm("tom");
  ft->mutable_sched_switch()->set_prev_pid(123);
  ft->mutable_sched_switch()->set_next_comm("jerry");
  ft->mutable_sched_switch()->set_next_pid(456);
  proto.mutable_for_testing()->set_str("foo");
  std::string ser_buf = proto.SerializeAsString();

  for (size_t i = 0; i < ser_buf.size(); i++) {
    ChunkSequence seq;
    seq.emplace_back(&ser_buf[0], i);
    seq.emplace_back(&ser_buf[i], ser_buf.size() - i);
    PacketStreamValidator validator(&seq);
    EXPECT_FALSE(validator.Validate());
  }
}

TEST(PacketStreamValidatorTest, TruncatedPacket) {
  protos::TracePacket proto;
  proto.mutable_for_testing()->set_str("string field");
  std::string ser_buf = proto.SerializeAsString();

  for (size_t i = 1; i < ser_buf.size(); i++) {
    ChunkSequence seq;
    seq.emplace_back(&ser_buf[0], i);
    PacketStreamValidator validator(&seq);
    EXPECT_FALSE(validator.Validate());
  }
}

TEST(PacketStreamValidatorTest, TrailingGarbage) {
  protos::TracePacket proto;
  proto.mutable_for_testing()->set_str("string field");
  std::string ser_buf = proto.SerializeAsString();
  ser_buf += "bike is short for bichael";

  ChunkSequence seq;
  seq.emplace_back(&ser_buf[0], ser_buf.size());
  PacketStreamValidator validator(&seq);
  EXPECT_FALSE(validator.Validate());
}

}  // namespace
}  // namespace perfetto
