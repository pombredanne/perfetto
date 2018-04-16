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
#include "src/process_stats/procfs_utils.h"

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
  TraceWriter::TracePacketHandle trace_packet{};
  std::set<int32_t>* seen_pids = &seen_pids_;

  file_utils::ForEachPidInProcPath(
      "/proc", [this, &trace_packet, seen_pids](int pid) {
        // ForEachPid will list all processes and
        // threads. Here we want to iterate first
        // only by processes (for which pid ==
        // thread group id)
        char path[64];
        sprintf(path, "/proc/%d/task", pid);
        if (file_utils::GetFirstNumericDirectoryInPath(path) != pid)
          return;

        WriteProcess(pid, &trace_packet);
        seen_pids->insert(pid);
      });
}

void ProcessStatsDataSource::OnPids(const std::vector<int32_t>& pids) {
  TraceWriter::TracePacketHandle trace_packet{};
  for (int32_t pid : pids) {
    auto it_and_inserted = seen_pids_.emplace(pid);
    if (!it_and_inserted.second)
      continue;
    WriteProcess(pid, &trace_packet);
  }
}

void ProcessStatsDataSource::Flush() {
  writer_->Flush();
}

// To be overidden for testing.
std::unique_ptr<ProcessInfo> ProcessStatsDataSource::ReadProcessInfo(int pid) {
  return procfs_utils::ReadProcessInfo(pid);
}

void ProcessStatsDataSource::WriteProcess(
    int32_t pid,
    TraceWriter::TracePacketHandle* handle) {
  // Check if this the main thread. We only want to read process info for
  // the main thread. Child thread info will be containined in it.
  char path[64];
  sprintf(path, "/proc/%d/task", pid);
  // A pid is the main thread if it is the first directory in /proc/*pid*/task
  int main_thread = file_utils::GetFirstNumericDirectoryInPath(path);
  if (main_thread == -1) {  // pid no longer exists.
    seen_pids_.erase(pid);  // So if it is seen again, can try to grab info.
    return;
  }

  if (pid != main_thread) {
    // Current pid is not the main thread.
    auto it_and_inserted = seen_pids_.emplace(main_thread);
    if (it_and_inserted.second) {
      WriteProcess(main_thread, handle);
    }
    return;
  }

  // At this point we definitely have the main thread pid.
  std::unique_ptr<ProcessInfo> process = ReadProcessInfo(pid);

  if (!*handle) {
    *handle = writer_->NewTracePacket();
    process_tree_ = (*handle)->set_process_tree();
  }

  procfs_utils::ReadProcessThreads(process.get());
  auto* process_writer = process_tree_->add_processes();
  process_writer->set_pid(process->pid);
  process_writer->set_ppid(process->ppid);
  for (const auto& field : process->cmdline)
    process_writer->add_cmdline(field.c_str());
  for (auto& thread : process->threads) {
    auto* thread_writer = process_writer->add_threads();
    seen_pids_.emplace(thread.second.tid);
    thread_writer->set_tid(thread.second.tid);
    thread_writer->set_name(thread.second.name);
  }
}

}  // namespace perfetto
