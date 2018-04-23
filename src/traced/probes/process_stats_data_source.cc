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

#include <utility>

#include "perfetto/trace/trace_packet.pbzero.h"
#include "src/process_stats/file_utils.h"

namespace perfetto {

ProcessStatsDataSource::ProcessStatsDataSource(
    TracingSessionID id,
    std::unique_ptr<TraceWriter> writer,
    const DataSourceConfig& config)
    : session_id_(id),
      writer_(std::move(writer)),
      config_(config),
      weak_factory_(this) {}

ProcessStatsDataSource::~ProcessStatsDataSource() = default;

base::WeakPtr<ProcessStatsDataSource> ProcessStatsDataSource::GetWeakPtr()
    const {
  return weak_factory_.GetWeakPtr();
}

void ProcessStatsDataSource::WriteAllProcesses() {
  auto trace_packet = writer_->NewTracePacket();
  auto* process_tree = trace_packet->set_process_tree();

  file_utils::ForEachPidInProcPath("/proc", [this, process_tree](int pid) {
    WriteProcess(pid, process_tree);
  });
}

void ProcessStatsDataSource::OnPids(const std::vector<int32_t>& pids) {
  TraceWriter::TracePacketHandle trace_packet{};
  protos::pbzero::ProcessTree* process_tree = nullptr;

  // Note that the notion of a PID for the Linux kernel corresponds to what most
  // humans typically refer to as TID (Thread ID).

  for (int32_t pid : pids) {
    if (seen_pids_.count(pid))
      continue;
    if (!process_tree) {
      trace_packet = writer_->NewTracePacket();
      process_tree = trace_packet->set_process_tree();
    }
    WriteProcess(pid, process_tree);
  }
}

void ProcessStatsDataSource::Flush() {
  writer_->Flush();
}

// To be overidden for testing.
bool ProcessStatsDataSource::ReadProcessInfo(
    int pid,
    procfs_utils::ProcessInfo* process) {
  return procfs_utils::ReadProcessInfo(pid, process);
}

void ProcessStatsDataSource::WriteProcess(int32_t pid,
                                          protos::pbzero::ProcessTree* tree) {
  procfs_utils::ProcessInfo process;
  if (!ReadProcessInfo(pid, &process))
    return;
  // Note: process.pid might not match |pid|, if pid was a thread id.
  auto* process_writer = tree->add_processes();
  process_writer->set_pid(process.pid);
  process_writer->set_ppid(process.ppid);
  seen_pids_.emplace(process.pid);

  for (const auto& field : process.cmdline)
    process_writer->add_cmdline(field.c_str());

  for (auto& thread : process.threads) {
    auto* thread_writer = process_writer->add_threads();
    thread_writer->set_tid(thread.second.tid);
    thread_writer->set_name(thread.second.name);
    seen_pids_.emplace(thread.second.tid);
  }

  PERFETTO_DCHECK(seen_pids_.count(pid) == 1);
}

}  // namespace perfetto
