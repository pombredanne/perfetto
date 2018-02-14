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

#include "src/traced/probes/producer_impl.h"

#include <stdio.h>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"

#include "src/process_stats/file_utils.h"
#include "src/process_stats/procfs_utils.h"

#include "perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace {

uint64_t kInitialConnectionBackoffMs = 100;
uint64_t kMaxConnectionBackoffMs = 30 * 1000;

}  // namespace.

// State transition diagram:
//                    +----------------------------+
//                    v                            +
// NotStarted -> NotConnected -> Connecting -> Connected
//                    ^              +
//                    +--------------+
//

ProducerImpl::~ProducerImpl() = default;

void ProducerImpl::OnConnect() {
  PERFETTO_DCHECK(state_ == kConnecting);
  state_ = kConnected;
  ResetConnectionBackoff();
  PERFETTO_LOG("Connected to the service");

  DataSourceDescriptor descriptor;
  descriptor.set_name("com.google.perfetto.ftrace");
  endpoint_->RegisterDataSource(
      descriptor, [this](DataSourceID id) { data_source_id_ = id; });
  descriptor.set_name("com.google.perfetto.process_stats");
  endpoint_->RegisterDataSource(
      descriptor, [this](DataSourceID id) { data_source_id_ = id; });
}

void ProducerImpl::OnDisconnect() {
  PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
  state_ = kNotConnected;
  PERFETTO_LOG("Disconnected from tracing service");
  IncreaseConnectionBackoff();

  task_runner_->PostDelayedTask([this] { this->Connect(); },
                                connection_backoff_ms_);
}

void ProducerImpl::CreateDataSourceInstance(
    DataSourceInstanceID id,
    const DataSourceConfig& source_config) {
  // Don't retry if FtraceController::Create() failed once.
  // This can legitimately happen on user builds where we cannot access the
  // debug paths, e.g., because of SELinux rules.
  if (source_config.name() == "com.google.perfetto.ftrace") {
    if (ftrace_creation_failed_)
      return;

    // Lazily create on the first instance.
    if (!ftrace_) {
      ftrace_ = FtraceController::Create(task_runner_);

      if (!ftrace_) {
        PERFETTO_ELOG("Failed to create FtraceController");
        ftrace_creation_failed_ = true;
        return;
      }

      ftrace_->DisableAllEvents();
      ftrace_->ClearTrace();
    }

    PERFETTO_LOG("Ftrace start (id=%" PRIu64 ", target_buf=%" PRIu32 ")", id,
                 source_config.target_buffer());

    // TODO(hjd): Would be nice if ftrace_reader could use the generated config.
    DataSourceConfig::FtraceConfig proto_config = source_config.ftrace_config();

    // TODO(hjd): Static cast is bad, target_buffer() should return a BufferID.
    auto trace_writer = endpoint_->CreateTraceWriter(
        static_cast<BufferID>(source_config.target_buffer()));
    auto delegate = std::unique_ptr<SinkDelegate>(
        new SinkDelegate(std::move(trace_writer)));
    auto sink = ftrace_->CreateSink(std::move(proto_config), delegate.get());
    PERFETTO_CHECK(sink);
    delegate->sink(std::move(sink));
    delegates_.emplace(id, std::move(delegate));

  } else {  // process stats

    auto trace_writer = endpoint_->CreateTraceWriter(
        static_cast<BufferID>(source_config.target_buffer()));
    procfs_utils::ProcessMap processes;
    auto trace_packet = trace_writer->NewTracePacket();
    protos::pbzero::ProcessDataBundle* bundle =
        trace_packet->set_process_data_bundle();

    file_utils::ForEachPidInProcPath("/proc", [&processes, &bundle](int pid) {
      if (!processes.count(pid)) {
        if (procfs_utils::ReadTgid(pid) != pid)
          return;
        processes[pid] = procfs_utils::ReadProcessInfo(pid);
      }
      ProcessInfo* process = processes[pid].get();
      procfs_utils::ReadProcessThreads(process);
      protos::pbzero::ProcessData* process_writer = bundle->add_processes();
      process_writer->set_name(process->name);
      process_writer->set_pid(process->pid);
      process_writer->set_in_kernel(process->in_kernel);
      process_writer->set_is_app(process->is_app);

      for (auto& thread : process->threads) {
        protos::pbzero::ProcessData::ThreadData* thread_writer =
            process_writer->add_threads();
        thread_writer->set_tid(thread.second.tid);
        thread_writer->set_name(thread.second.name);
      }
    });
    trace_packet->Finalize();
  }
}

void ProducerImpl::TearDownDataSourceInstance(DataSourceInstanceID id) {
  PERFETTO_LOG("Producer stop (id=%" PRIu64 ")", id);
  delegates_.erase(id);
}

void ProducerImpl::ConnectWithRetries(const char* socket_name,
                                      base::TaskRunner* task_runner) {
  PERFETTO_DCHECK(state_ == kNotStarted);
  state_ = kNotConnected;

  ResetConnectionBackoff();
  socket_name_ = socket_name;
  task_runner_ = task_runner;
  Connect();
}

void ProducerImpl::Connect() {
  PERFETTO_DCHECK(state_ == kNotConnected);
  state_ = kConnecting;
  endpoint_ = ProducerIPCClient::Connect(socket_name_, this, task_runner_);
}

void ProducerImpl::IncreaseConnectionBackoff() {
  connection_backoff_ms_ *= 2;
  if (connection_backoff_ms_ > kMaxConnectionBackoffMs)
    connection_backoff_ms_ = kMaxConnectionBackoffMs;
}

void ProducerImpl::ResetConnectionBackoff() {
  connection_backoff_ms_ = kInitialConnectionBackoffMs;
}

ProducerImpl::SinkDelegate::SinkDelegate(std::unique_ptr<TraceWriter> writer)
    : writer_(std::move(writer)) {}

ProducerImpl::SinkDelegate::~SinkDelegate() = default;

ProducerImpl::FtraceBundleHandle ProducerImpl::SinkDelegate::GetBundleForCpu(
    size_t) {
  trace_packet_ = writer_->NewTracePacket();
  return FtraceBundleHandle(trace_packet_->set_ftrace_events());
}

void ProducerImpl::SinkDelegate::OnBundleComplete(size_t, FtraceBundleHandle) {
  trace_packet_->Finalize();
}

}  // namespace perfetto
