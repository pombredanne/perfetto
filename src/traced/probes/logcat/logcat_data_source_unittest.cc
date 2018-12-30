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

#include "src/traced/probes/logcat/logcat_data_source.h"

#include "src/base/test/test_task_runner.h"
#include "src/tracing/core/trace_writer_for_testing.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "perfetto/trace/trace_packet.pb.h"
#include "perfetto/trace/trace_packet.pbzero.h"

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Invoke;
using ::testing::Return;

namespace perfetto {
namespace {

class TestLogcatDataSource : public LogcatDataSource {
 public:
  TestLogcatDataSource(const DataSourceConfig& config,
                       base::TaskRunner* task_runner,
                       TracingSessionID id,
                       std::unique_ptr<TraceWriter> writer)
      : LogcatDataSource(config, task_runner, id, std::move(writer)) {}

  MOCK_METHOD0(ReadEventLogDefinitions, std::string());
  MOCK_METHOD0(ConnectLogdrSocket, base::UnixSocketRaw());
};

class LogcatDataSourceTest : public ::testing::Test {
 protected:
  LogcatDataSourceTest() {}

  void CreateInstance(const DataSourceConfig& cfg) {
    auto writer =
        std::unique_ptr<TraceWriterForTesting>(new TraceWriterForTesting());
    writer_raw_ = writer.get();
    data_source_.reset(
        new TestLogcatDataSource(cfg, &task_runner_, 0, std::move(writer)));
  }

  void StartAndSimulateLogd(
      const std::vector<std::vector<uint8_t>>& fake_events) {
    base::UnixSocketRaw send_sock;
    base::UnixSocketRaw recv_sock;
    // In theory this should be a kSeqPacket. We use kDgram here so that the
    // test can run also on MacOS (which doesn't support SOCK_SEQPACKET).
    const auto kSockType = base::SockType::kDgram;
    std::tie(send_sock, recv_sock) = base::UnixSocketRaw::CreatePair(kSockType);
    ASSERT_TRUE(send_sock);
    ASSERT_TRUE(recv_sock);

    EXPECT_CALL(*data_source_, ConnectLogdrSocket())
        .WillOnce(Invoke([&recv_sock] { return std::move(recv_sock); }));

    data_source_->Start();

    char cmd[64]{};
    EXPECT_GT(send_sock.Receive(cmd, sizeof(cmd) - 1), 0);
    EXPECT_STREQ("stream lids=0,2,3,4,7", cmd);

    // Send back logcat messages emulating the logdr socket.
    for (const auto& buf : fake_events)
      send_sock.Send(buf.data(), buf.size());

    auto on_flush = task_runner_.CreateCheckpoint("on_flush");
    data_source_->Flush(1, on_flush);
    task_runner_.RunUntilCheckpoint("on_flush");
  }

  base::TestTaskRunner task_runner_;
  std::unique_ptr<TestLogcatDataSource> data_source_;
  TraceWriterForTesting* writer_raw_;

  const std::vector<std::vector<uint8_t>> kValidTextEvents{
      // 12-29 23:13:59.679  7546  8991 I ActivityManager:
      // Killing 11660:com.google.android.videos/u0a168 (adj 985): empty #17
      {0x55, 0x00, 0x1c, 0x00, 0x7a, 0x1d, 0x00, 0x00, 0x1f, 0x23, 0x00, 0x00,
       0xb7, 0xff, 0x27, 0x5c, 0xe6, 0x58, 0x7b, 0x28, 0x03, 0x00, 0x00, 0x00,
       0xe8, 0x03, 0x00, 0x00, 0x04, 0x41, 0x63, 0x74, 0x69, 0x76, 0x69, 0x74,
       0x79, 0x4d, 0x61, 0x6e, 0x61, 0x67, 0x65, 0x72, 0x00, 0x4b, 0x69, 0x6c,
       0x6c, 0x69, 0x6e, 0x67, 0x20, 0x31, 0x31, 0x36, 0x36, 0x30, 0x3a, 0x63,
       0x6f, 0x6d, 0x2e, 0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x2e, 0x61, 0x6e,
       0x64, 0x72, 0x6f, 0x69, 0x64, 0x2e, 0x76, 0x69, 0x64, 0x65, 0x6f, 0x73,
       0x2f, 0x75, 0x30, 0x61, 0x31, 0x36, 0x38, 0x20, 0x28, 0x61, 0x64, 0x6a,
       0x20, 0x39, 0x38, 0x35, 0x29, 0x3a, 0x20, 0x65, 0x6d, 0x70, 0x74, 0x79,
       0x20, 0x23, 0x31, 0x37, 0x00},

      // 12-29 23:13:59.683  7546  7570 W libprocessgroup:
      // kill(-11660, 9) failed: No such process
      {0x39, 0x00, 0x1c, 0x00, 0x7a, 0x1d, 0x00, 0x00, 0x92, 0x1d, 0x00,
       0x00, 0xb7, 0xff, 0x27, 0x5c, 0x12, 0xf3, 0xbd, 0x28, 0x00, 0x00,
       0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x05, 0x6c, 0x69, 0x62, 0x70,
       0x72, 0x6f, 0x63, 0x65, 0x73, 0x73, 0x67, 0x72, 0x6f, 0x75, 0x70,
       0x00, 0x6b, 0x69, 0x6c, 0x6c, 0x28, 0x2d, 0x31, 0x31, 0x36, 0x36,
       0x30, 0x2c, 0x20, 0x39, 0x29, 0x20, 0x66, 0x61, 0x69, 0x6c, 0x65,
       0x64, 0x3a, 0x20, 0x4e, 0x6f, 0x20, 0x73, 0x75, 0x63, 0x68, 0x20,
       0x70, 0x72, 0x6f, 0x63, 0x65, 0x73, 0x73, 0x00},

      // 12-29 23:13:59.719  7415  7415 I Zygote:
      // Process 11660 exited due to signal (9)
      {0x2f, 0x00, 0x1c, 0x00, 0xf7, 0x1c, 0x00, 0x00, 0xf7, 0x1c, 0x00,
       0x00, 0xb7, 0xff, 0x27, 0x5c, 0x7c, 0x11, 0xe2, 0x2a, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x5a, 0x79, 0x67, 0x6f,
       0x74, 0x65, 0x00, 0x50, 0x72, 0x6f, 0x63, 0x65, 0x73, 0x73, 0x20,
       0x31, 0x31, 0x36, 0x36, 0x30, 0x20, 0x65, 0x78, 0x69, 0x74, 0x65,
       0x64, 0x20, 0x64, 0x75, 0x65, 0x20, 0x74, 0x6f, 0x20, 0x73, 0x69,
       0x67, 0x6e, 0x61, 0x6c, 0x20, 0x28, 0x39, 0x29, 0x00},
  };
};  // namespace

TEST_F(LogcatDataSourceTest, ParseEventLogDefinitions) {
  CreateInstance(DataSourceConfig());
  static const char kContents[] = R"(
42 answer (to life the universe etc|3)
314 pi
1003 auditd (avc|3)
1004 chatty (dropped|3)
1005 tag_def (tag|1),(name|3),(format|3)
2718 e
2732 storaged_disk_stats (type|3),(start_time|2|3),(end_time|2|3),(read_ios|2|1),(read_merges|2|1),(read_sectors|2|1),(read_ticks|2|3),(write_ios|2|1),(write_merges|2|1),(write_sectors|2|1),(write_ticks|2|3),(o_in_flight|2|1),(io_ticks|2|3),(io_in_queue|2|1)
invalid_line (
9999 invalid_line2 (
1937006964 stats_log (atom_id|1|5),(data|4)
)";
  EXPECT_CALL(*data_source_, ReadEventLogDefinitions())
      .WillOnce(Return(kContents));
  data_source_->ParseEventLogDefinitions();

  auto* fmt = data_source_->GetEventFormat(42);
  ASSERT_NE(fmt, nullptr);
  ASSERT_EQ(fmt->name, "answer");
  ASSERT_EQ(fmt->fields.size(), 1);
  ASSERT_EQ(fmt->fields[0], "to life the universe etc");

  fmt = data_source_->GetEventFormat(314);
  ASSERT_NE(fmt, nullptr);
  ASSERT_EQ(fmt->name, "pi");
  ASSERT_EQ(fmt->fields.size(), 0);

  fmt = data_source_->GetEventFormat(1005);
  ASSERT_NE(fmt, nullptr);
  ASSERT_EQ(fmt->name, "tag_def");
  ASSERT_EQ(fmt->fields.size(), 3);
  ASSERT_EQ(fmt->fields[0], "tag");
  ASSERT_EQ(fmt->fields[1], "name");
  ASSERT_EQ(fmt->fields[2], "format");

  fmt = data_source_->GetEventFormat(1937006964);
  ASSERT_NE(fmt, nullptr);
  ASSERT_EQ(fmt->name, "stats_log");
  ASSERT_EQ(fmt->fields.size(), 2);
  ASSERT_EQ(fmt->fields[0], "atom_id");
  ASSERT_EQ(fmt->fields[1], "data");
}

TEST_F(LogcatDataSourceTest, TextEvents) {
  DataSourceConfig cfg;
  CreateInstance(cfg);
  EXPECT_CALL(*data_source_, ReadEventLogDefinitions()).WillOnce(Return(""));
  StartAndSimulateLogd(kValidTextEvents);

  // Read back the data that would have been written into the trace.

  auto packet = writer_raw_->ParseProto();
  ASSERT_TRUE(packet);
  EXPECT_TRUE(packet->has_logcat());
  EXPECT_EQ(packet->logcat().events_size(), 3);

  const auto& decoded = packet->logcat().events();

  EXPECT_EQ(decoded.Get(0).log_id(), protos::AndroidLogcatLogId::LID_SYSTEM);
  EXPECT_EQ(decoded.Get(0).pid(), 7546);
  EXPECT_EQ(decoded.Get(0).tid(), 8991);
  EXPECT_EQ(decoded.Get(0).prio(), protos::AndroidLogcatPriority::PRIO_INFO);
  EXPECT_EQ(decoded.Get(0).timestamp(), 1546125239679172326LL);
  EXPECT_EQ(decoded.Get(0).tag(), "ActivityManager");
  EXPECT_EQ(
      decoded.Get(0).message(),
      "Killing 11660:com.google.android.videos/u0a168 (adj 985): empty #17");

  EXPECT_EQ(decoded.Get(1).log_id(), protos::AndroidLogcatLogId::LID_DEFAULT);
  EXPECT_EQ(decoded.Get(1).pid(), 7546);
  EXPECT_EQ(decoded.Get(1).tid(), 7570);
  EXPECT_EQ(decoded.Get(1).prio(), protos::AndroidLogcatPriority::PRIO_WARN);
  EXPECT_EQ(decoded.Get(1).timestamp(), 1546125239683537170LL);
  EXPECT_EQ(decoded.Get(1).tag(), "libprocessgroup");
  EXPECT_EQ(decoded.Get(1).message(),
            "kill(-11660, 9) failed: No such process");

  EXPECT_EQ(decoded.Get(2).log_id(), protos::AndroidLogcatLogId::LID_DEFAULT);
  EXPECT_EQ(decoded.Get(2).pid(), 7415);
  EXPECT_EQ(decoded.Get(2).tid(), 7415);
  EXPECT_EQ(decoded.Get(2).prio(), protos::AndroidLogcatPriority::PRIO_INFO);
  EXPECT_EQ(decoded.Get(2).timestamp(), 1546125239719458684LL);
  EXPECT_EQ(decoded.Get(2).tag(), "Zygote");
  EXPECT_EQ(decoded.Get(2).message(), "Process 11660 exited due to signal (9)");
}

TEST_F(LogcatDataSourceTest, TextEventsFilterTag) {
  DataSourceConfig cfg;
  *cfg.mutable_android_logcat_config()->add_filter_tags() = "Zygote";
  *cfg.mutable_android_logcat_config()->add_filter_tags() = "ActivityManager";
  *cfg.mutable_android_logcat_config()->add_filter_tags() = "Unmatched";
  *cfg.mutable_android_logcat_config()->add_filter_tags() = "libprocessgroupZZ";

  CreateInstance(cfg);
  EXPECT_CALL(*data_source_, ReadEventLogDefinitions()).WillOnce(Return(""));
  StartAndSimulateLogd(kValidTextEvents);

  auto packet = writer_raw_->ParseProto();
  ASSERT_TRUE(packet);
  EXPECT_TRUE(packet->has_logcat());
  EXPECT_EQ(packet->logcat().events_size(), 2);

  const auto& decoded = packet->logcat().events();
  EXPECT_EQ(decoded.Get(0).tag(), "ActivityManager");
  EXPECT_EQ(decoded.Get(1).tag(), "Zygote");
}

TEST_F(LogcatDataSourceTest, TextEventsFilterPrio) {
  DataSourceConfig cfg;
  cfg.mutable_android_logcat_config()->set_min_prio(
      AndroidLogcatConfig::AndroidLogcatPriority::PRIO_WARN);

  CreateInstance(cfg);
  EXPECT_CALL(*data_source_, ReadEventLogDefinitions()).WillOnce(Return(""));
  StartAndSimulateLogd(kValidTextEvents);

  auto packet = writer_raw_->ParseProto();
  ASSERT_TRUE(packet);
  EXPECT_TRUE(packet->has_logcat());
  EXPECT_EQ(packet->logcat().events_size(), 1);

  const auto& decoded = packet->logcat().events();
  EXPECT_EQ(decoded.Get(0).tag(), "libprocessgroup");
}

}  // namespace
}  // namespace perfetto
