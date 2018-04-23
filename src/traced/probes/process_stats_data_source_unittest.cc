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

#include "src/traced/probes/process_stats_data_source.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "perfetto/trace/trace_packet.pb.h"
#include "perfetto/trace/trace_packet.pbzero.h"
#include "src/tracing/core/trace_writer_for_testing.h"

using ::procfs_utils::ProcessInfo;
using ::testing::_;
using ::testing::Invoke;

namespace perfetto {
namespace {

class TestProcessStatsDataSource : public ProcessStatsDataSource {
 public:
  TestProcessStatsDataSource(TracingSessionID id,
                             std::unique_ptr<TraceWriter> writer,
                             const DataSourceConfig& config)
      : ProcessStatsDataSource(id, std::move(writer), config) {}

  MOCK_METHOD2(ReadProcessInfo, bool(int pid, ProcessInfo*));
};

class ProcessStatsDataSourceTest : public ::testing::Test {
 protected:
  ProcessStatsDataSourceTest() {}

  TraceWriterForTesting* writer_raw_;

  std::unique_ptr<TestProcessStatsDataSource> GetProcessStatsDataSource(
      const DataSourceConfig& cfg) {
    auto writer =
        std::unique_ptr<TraceWriterForTesting>(new TraceWriterForTesting());
    writer_raw_ = writer.get();
    return std::unique_ptr<TestProcessStatsDataSource>(
        new TestProcessStatsDataSource(0, std::move(writer), cfg));
  }
};

TEST_F(ProcessStatsDataSourceTest, WriteOnDemand) {
  auto data_source = GetProcessStatsDataSource(DataSourceConfig());
  EXPECT_CALL(*data_source, ReadProcessInfo(42, _))
      .WillRepeatedly(Invoke([](int pid, ProcessInfo* process) {
        process->pid = pid;
        process->in_kernel = true;  // So that it doesn't try to read threads.
        process->ppid = 0;
        process->cmdline.push_back("test_process");
        return true;
      }));
  data_source->OnPids({42});
  std::unique_ptr<protos::TracePacket> packet = writer_raw_->ParseProto();
  ASSERT_TRUE(packet->has_process_tree());
  ASSERT_EQ(packet->process_tree().processes_size(), 1);
  auto first_process = packet->process_tree().processes(0);
  ASSERT_EQ(first_process.pid(), 42);
  ASSERT_EQ(first_process.ppid(), 0);
  ASSERT_EQ(first_process.cmdline(0), std::string("test_process"));
}

TEST_F(ProcessStatsDataSourceTest, DontRescanCachedPIDsAndTIDs) {
  auto data_source = GetProcessStatsDataSource(DataSourceConfig());
  auto mock_info = [](int pid, ProcessInfo* process) {
    process->pid = pid;
    process->in_kernel = false;
    process->ppid = 0;
    process->cmdline.push_back("test_process");
    for (int tid = pid; tid < pid + 3; tid++) {
      auto* thread_info = &process->threads[tid];
      thread_info->tid = tid;
      strcpy(thread_info->name, "test_thread");
    }
    return true;
  };

  EXPECT_CALL(*data_source, ReadProcessInfo(10, _)).WillOnce(Invoke(mock_info));
  EXPECT_CALL(*data_source, ReadProcessInfo(20, _)).WillOnce(Invoke(mock_info));
  EXPECT_CALL(*data_source, ReadProcessInfo(30, _)).WillOnce(Invoke(mock_info));
  data_source->OnPids({10, 11, 12, 20, 21, 22, 10, 20, 11, 21});
  data_source->OnPids({30});
  data_source->OnPids({10, 30, 10, 31, 32});

  std::unique_ptr<protos::TracePacket> packet = writer_raw_->ParseProto();
  ASSERT_TRUE(packet->has_process_tree());
  const auto& proceses = packet->process_tree().processes();
  ASSERT_EQ(proceses.size(), 3);
  for (int pid_idx = 0; pid_idx < 3; pid_idx++) {
    int pid = (pid_idx + 1) * 10;
    ASSERT_EQ(proceses.Get(pid_idx).pid(), pid);
    for (int tid_idx = 0; tid_idx < 3; tid_idx++) {
      int tid = pid + tid_idx;
      ASSERT_EQ(proceses.Get(pid_idx).threads().Get(tid_idx).tid(), tid);
    }
  }
}

}  // namespace
}  // namespace perfetto
