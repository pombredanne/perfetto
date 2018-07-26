/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "src/traced/probes/ftrace/ftrace_data_source.h"

#include "src/traced/probes/ftrace/cpu_reader.h"
#include "src/traced/probes/ftrace/ftrace_controller.h"

#include "perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "perfetto/trace/ftrace/ftrace_stats.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

// static
constexpr int FtraceDataSource::kTypeId;

FtraceDataSource::FtraceDataSource(
    base::WeakPtr<FtraceController> controller_weak,
    TracingSessionID session_id,
    FtraceConfig config,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, kTypeId),
      config_(std::move(config)),
      writer_(std::move(writer)),
      controller_weak_(std::move(controller_weak)){};

FtraceDataSource::~FtraceDataSource() {
  if (controller_weak_)
    controller_weak_->RemoveDataSource(this);
};

void FtraceDataSource::Initialize(FtraceConfigId config_id,
                                  std::unique_ptr<EventFilter> event_filter) {
  config_id_ = config_id;
  event_filter_ = std::move(event_filter);
  DumpFtraceStats(&stats_before_);
}

void FtraceDataSource::DumpFtraceStats(FtraceStats* stats) {
  if (controller_weak_)
    controller_weak_->DumpFtraceStats(stats);
}

void FtraceDataSource::Flush() {
  // TODO(primiano): this still doesn't flush data from the kernel ftrace
  // buffers (see b/73886018). We should do that and delay the
  // NotifyFlushComplete() until the ftrace data has been drained from the
  // kernel ftrace buffer and written in the SMB.
  if (writer_ && (!trace_packet_ || trace_packet_->is_finalized())) {
    WriteStats();
    writer_->Flush();
  }
}

void FtraceDataSource::WriteStats() {
  {
    auto before_packet = writer_->NewTracePacket();
    auto out = before_packet->set_ftrace_stats();
    out->set_phase(protos::pbzero::FtraceStats_Phase_START_OF_TRACE);
    stats_before_.Write(out);
  }
  {
    FtraceStats stats_after{};
    DumpFtraceStats(&stats_after);
    auto after_packet = writer_->NewTracePacket();
    auto out = after_packet->set_ftrace_stats();
    out->set_phase(protos::pbzero::FtraceStats_Phase_END_OF_TRACE);
    stats_after.Write(out);
  }
}

void FtraceDataSource::OnBundleComplete() {
  trace_packet_->Finalize();  // TODO move this to
                              // FtraceController::OnRawFtraceDataAvailable
  /*
    if (file_source_ && !metadata.inode_and_device.empty()) {
      auto inodes = metadata.inode_and_device;
      auto weak_file_source = file_source_;
      task_runner_->PostTask([weak_file_source, inodes] {
        if (weak_file_source)
          weak_file_source->OnInodes(inodes);
      });
    }
    if (ps_source_ && !metadata.pids.empty()) {
      const auto& quirks = ps_source_->config().process_stats_config().quirks();
      if (std::find(quirks.begin(), quirks.end(),
                    ProcessStatsConfig::DISABLE_ON_DEMAND) != quirks.end()) {
        return;
      }
      const auto& pids = metadata.pids;
      auto weak_ps_source = ps_source_;
      task_runner_->PostTask([weak_ps_source, pids] {
        if (weak_ps_source)
          weak_ps_source->OnPids(pids);
      });
    }
    */
  metadata_.Clear();
}

FtraceDataSource::FtraceBundleHandle FtraceDataSource::GetBundleForCpu(size_t) {
  trace_packet_ = writer_->NewTracePacket();
  return FtraceBundleHandle(trace_packet_->set_ftrace_events());
}

}  // namespace perfetto
