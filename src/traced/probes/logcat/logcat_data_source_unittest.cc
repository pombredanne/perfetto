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
};

class LogcatDataSourceTest : public ::testing::Test {
 protected:
  LogcatDataSourceTest() {}

  std::unique_ptr<TestLogcatDataSource> CreateInstance(
      const DataSourceConfig& cfg) {
    auto writer =
        std::unique_ptr<TraceWriterForTesting>(new TraceWriterForTesting());
    writer_raw_ = writer.get();
    return std::unique_ptr<TestLogcatDataSource>(
        new TestLogcatDataSource(cfg, &task_runner_, 0, std::move(writer)));
  }

  base::TestTaskRunner task_runner_;
  TraceWriterForTesting* writer_raw_;
};

TEST_F(LogcatDataSourceTest, EventLogTagsParsing) {
  auto ds = CreateInstance(DataSourceConfig());
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
  EXPECT_CALL(*ds, ReadEventLogDefinitions()).WillOnce(Return(kContents));
  ds->ParseEventLogDefinitions();

  auto* fmt = ds->GetEventFormat(42);
  ASSERT_NE(fmt, nullptr);
  ASSERT_EQ(fmt->name, "answer");
  ASSERT_EQ(fmt->fields.size(), 1);
  ASSERT_EQ(fmt->fields[0], "to life the universe etc");

  fmt = ds->GetEventFormat(314);
  ASSERT_NE(fmt, nullptr);
  ASSERT_EQ(fmt->name, "pi");
  ASSERT_EQ(fmt->fields.size(), 0);

  fmt = ds->GetEventFormat(1005);
  ASSERT_NE(fmt, nullptr);
  ASSERT_EQ(fmt->name, "tag_def");
  ASSERT_EQ(fmt->fields.size(), 3);
  ASSERT_EQ(fmt->fields[0], "tag");
  ASSERT_EQ(fmt->fields[1], "name");
  ASSERT_EQ(fmt->fields[2], "format");

  fmt = ds->GetEventFormat(1937006964);
  ASSERT_NE(fmt, nullptr);
  ASSERT_EQ(fmt->name, "stats_log");
  ASSERT_EQ(fmt->fields.size(), 2);
  ASSERT_EQ(fmt->fields[0], "atom_id");
  ASSERT_EQ(fmt->fields[1], "data");
}

}  // namespace
}  // namespace perfetto
