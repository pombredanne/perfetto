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

#include "src/traced/probes/probes_producer.h"

#include <stdio.h>
#include <sys/stat.h>
#include <queue>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/weak_ptr.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/ftrace_config.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"

#include "perfetto/trace/filesystem/inode_file_map.pbzero.h"
#include "perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace {

uint64_t kInitialConnectionBackoffMs = 100;
uint64_t kMaxConnectionBackoffMs = 30 * 1000;
const char* kFtraceSourceName = "com.google.perfetto.ftrace";
const char* kProcessStatsSourceName = "com.google.perfetto.process_stats";
const char* kInodeFileMapSourceName = "com.google.perfetto.inode_file_map";

}  // namespace.

// State transition diagram:
//                    +----------------------------+
//                    v                            +
// NotStarted -> NotConnected -> Connecting -> Connected
//                    ^              +
//                    +--------------+
//

ProbesProducer::ProbesProducer() {}
ProbesProducer::~ProbesProducer() = default;

void ProbesProducer::OnConnect() {
  PERFETTO_DCHECK(state_ == kConnecting);
  state_ = kConnected;
  ResetConnectionBackoff();
  PERFETTO_LOG("Connected to the service");

  DataSourceDescriptor ftrace_descriptor;
  ftrace_descriptor.set_name(kFtraceSourceName);
  endpoint_->RegisterDataSource(ftrace_descriptor, [](DataSourceInstanceID) {});

  DataSourceDescriptor process_stats_descriptor;
  process_stats_descriptor.set_name(kProcessStatsSourceName);
  endpoint_->RegisterDataSource(process_stats_descriptor,
                                [](DataSourceInstanceID) {});

  DataSourceDescriptor inode_map_descriptor;
  inode_map_descriptor.set_name(kInodeFileMapSourceName);
  endpoint_->RegisterDataSource(inode_map_descriptor,
                                [](DataSourceInstanceID) {});
}

void ProbesProducer::OnDisconnect() {
  PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
  state_ = kNotConnected;
  PERFETTO_LOG("Disconnected from tracing service");
  IncreaseConnectionBackoff();

  // TODO(hjd): Erase all sinks and add e2e test for this.
  task_runner_->PostDelayedTask([this] { this->Connect(); },
                                connection_backoff_ms_);
}

void ProbesProducer::CreateDataSourceInstance(DataSourceInstanceID instance_id,
                                              const DataSourceConfig& config) {
  // TODO(hjd): This a hack since we don't actually know the session id. For
  // now we'll
  SessionID session_id = config.target_buffer();

  if (config.name() == kFtraceSourceName) {
    CreateFtraceDataSourceInstance(session_id, instance_id, config);
  } else if (config.name() == kInodeFileMapSourceName) {
    CreateInodeFileMapDataSourceInstance(session_id, instance_id, config);
  } else if (config.name() == kProcessStatsSourceName) {
    CreateProcessStatsDataSourceInstance(session_id, instance_id, config);
  } else {
    PERFETTO_ELOG("Data source name: %s not recognised.",
                  config.name().c_str());
    return;
  }

  std::map<SessionID, InodeFileMapDataSource*> file_sources;
  std::map<SessionID, ProcessStatsDataSource*> ps_sources;
  for (const auto& pair : file_map_sources_)
    file_sources[pair.second->GetSessionID()] = pair.second.get();
  for (const auto& pair : process_stats_sources_)
    ps_sources[pair.second->GetSessionID()] = pair.second.get();

  for (const auto& id_to_source : delegates_) {
    const auto& source = id_to_source.second;
    SessionID id = source->GetSessionID();
    if (!source->ps_source() && ps_sources.count(id))
      source->set_ps_source(ps_sources[id]->GetWeakPtr());
    if (!source->file_source() && file_sources.count(id))
      source->set_file_source(file_sources[id]->GetWeakPtr());
  }
}

void ProbesProducer::AddWatchdogsTimer(DataSourceInstanceID id,
                                       const DataSourceConfig& config) {
  if (config.trace_duration_ms() != 0)
    watchdogs_.emplace(id, base::Watchdog::GetInstance()->CreateFatalTimer(
                               5000 + 2 * config.trace_duration_ms()));
}

void ProbesProducer::CreateFtraceDataSourceInstance(
    SessionID session_id,
    DataSourceInstanceID id,
    const DataSourceConfig& config) {
  // Don't retry if FtraceController::Create() failed once.
  // This can legitimately happen on user builds where we cannot access the
  // debug paths, e.g., because of SELinux rules.
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
               config.target_buffer());

  FtraceConfig proto_config = config.ftrace_config();

  // TODO(hjd): Static cast is bad, target_buffer() should return a BufferID.
  auto trace_writer = endpoint_->CreateTraceWriter(
      static_cast<BufferID>(config.target_buffer()));
  auto delegate = std::unique_ptr<SinkDelegate>(
      new SinkDelegate(session_id, task_runner_, std::move(trace_writer)));
  auto sink = ftrace_->CreateSink(std::move(proto_config), delegate.get());
  if (!sink) {
    PERFETTO_ELOG("Failed to start tracing (maybe someone else is using it?)");
    return;
  }
  delegate->set_sink(std::move(sink));
  delegates_.emplace(id, std::move(delegate));
  AddWatchdogsTimer(id, config);
}

void ProbesProducer::CreateInodeFileMapDataSourceInstance(
    SessionID session_id,
    DataSourceInstanceID id,
    const DataSourceConfig& config) {
  PERFETTO_LOG("Inode file map start (id=%" PRIu64 ", target_buf=%" PRIu32 ")",
               id, config.target_buffer());
  auto trace_writer = endpoint_->CreateTraceWriter(
      static_cast<BufferID>(config.target_buffer()));
  CreateDeviceToInodeMap("/system/", &system_inodes_);
  auto file_map_source =
      std::unique_ptr<InodeFileMapDataSource>(new InodeFileMapDataSource(
          session_id, &system_inodes_, std::move(trace_writer)));
  file_map_sources_.emplace(id, std::move(file_map_source));
  AddWatchdogsTimer(id, config);
}

void ProbesProducer::CreateProcessStatsDataSourceInstance(
    SessionID session_id,
    DataSourceInstanceID id,
    const DataSourceConfig& config) {
  PERFETTO_DCHECK(process_stats_sources_.count(id) == 0);
  auto trace_writer = endpoint_->CreateTraceWriter(
      static_cast<BufferID>(config.target_buffer()));
  auto source = std::unique_ptr<ProcessStatsDataSource>(
      new ProcessStatsDataSource(session_id, std::move(trace_writer)));
  auto it_and_inserted = process_stats_sources_.emplace(id, std::move(source));
  PERFETTO_DCHECK(it_and_inserted.second);
  it_and_inserted.first->second->WriteAllProcesses();
}

// static
void ProbesProducer::CreateDeviceToInodeMap(
    const std::string& root_directory,
    std::map<uint32_t, InodeMap>* block_device_map) {
  // Return immediately if we've already filled in the system map
  if (!block_device_map->empty())
    return;
  std::queue<std::string> queue;
  queue.push(root_directory);
  while (!queue.empty()) {
    struct dirent* entry;
    std::string filepath = queue.front();
    queue.pop();
    DIR* dir = opendir(filepath.c_str());
    filepath += "/";
    if (dir == nullptr)
      continue;
    while ((entry = readdir(dir)) != nullptr) {
      std::string filename = entry->d_name;
      if (filename == "." || filename == "..")
        continue;
      uint64_t inode_number = entry->d_ino;
      struct stat buf;
      if (lstat(filepath.c_str(), &buf) != 0)
        continue;
      uint32_t block_device_id = buf.st_dev;
      InodeMap& inode_map = (*block_device_map)[block_device_id];
      // Default
      Type type = protos::pbzero::InodeFileMap_Entry_Type_UNKNOWN;
      // Readdir and stat not guaranteed to have directory info for all systems
      if (entry->d_type == DT_DIR || S_ISDIR(buf.st_mode)) {
        // Continue iterating through files if current entry is a directory
        queue.push(filepath + filename);
        type = protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY;
      } else if (entry->d_type == DT_REG || S_ISREG(buf.st_mode)) {
        type = protos::pbzero::InodeFileMap_Entry_Type_FILE;
      }
      inode_map[inode_number].first = type;
      inode_map[inode_number].second.emplace(filepath + filename);
    }
    closedir(dir);
  }
}

void ProbesProducer::TearDownDataSourceInstance(DataSourceInstanceID id) {
  PERFETTO_LOG("Producer stop (id=%" PRIu64 ")", id);
  // |id| could be the id of any of the datasources we handle:
  PERFETTO_DCHECK((delegates_.count(id) + process_stats_sources_.count(id) +
                   file_map_sources_.count(id)) == 1);
  delegates_.erase(id);
  process_stats_sources_.erase(id);
  file_map_sources_.erase(id);
  watchdogs_.erase(id);
}

void ProbesProducer::ConnectWithRetries(const char* socket_name,
                                        base::TaskRunner* task_runner) {
  PERFETTO_DCHECK(state_ == kNotStarted);
  state_ = kNotConnected;

  ResetConnectionBackoff();
  socket_name_ = socket_name;
  task_runner_ = task_runner;
  Connect();
}

void ProbesProducer::Connect() {
  PERFETTO_DCHECK(state_ == kNotConnected);
  state_ = kConnecting;
  endpoint_ = ProducerIPCClient::Connect(socket_name_, this, task_runner_);
}

void ProbesProducer::IncreaseConnectionBackoff() {
  connection_backoff_ms_ *= 2;
  if (connection_backoff_ms_ > kMaxConnectionBackoffMs)
    connection_backoff_ms_ = kMaxConnectionBackoffMs;
}

void ProbesProducer::ResetConnectionBackoff() {
  connection_backoff_ms_ = kInitialConnectionBackoffMs;
}

ProbesProducer::SinkDelegate::SinkDelegate(SessionID id,
                                           base::TaskRunner* task_runner,
                                           std::unique_ptr<TraceWriter> writer)
    : session_id_(id),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {}

ProbesProducer::SinkDelegate::~SinkDelegate() = default;

SessionID ProbesProducer::SinkDelegate::GetSessionID() const {
  return session_id_;
}

ProbesProducer::FtraceBundleHandle
ProbesProducer::SinkDelegate::GetBundleForCpu(size_t) {
  trace_packet_ = writer_->NewTracePacket();
  return FtraceBundleHandle(trace_packet_->set_ftrace_events());
}

void ProbesProducer::SinkDelegate::OnBundleComplete(
    size_t,
    FtraceBundleHandle,
    const FtraceMetadata& metadata) {
  trace_packet_->Finalize();

  if (file_source_ && !metadata.inodes.empty()) {
    auto inodes = metadata.inodes;
    auto weak = file_source_;
    task_runner_->PostTask([weak, inodes] {
      if (weak)
        weak->OnInodes(inodes);
    });
  }
}

ProbesProducer::InodeFileMapDataSource::InodeFileMapDataSource(
    SessionID id,
    std::map<uint32_t, InodeMap>* file_system_inodes,
    std::unique_ptr<TraceWriter> writer)
    : session_id_(id),
      file_system_inodes_(file_system_inodes),
      writer_(std::move(writer)),
      weak_factory_(this) {}

ProbesProducer::InodeFileMapDataSource::~InodeFileMapDataSource() = default;

SessionID ProbesProducer::InodeFileMapDataSource::GetSessionID() const {
  return session_id_;
}

base::WeakPtr<ProbesProducer::InodeFileMapDataSource>
ProbesProducer::InodeFileMapDataSource::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

void ProbesProducer::InodeFileMapDataSource::WriteInodes(
    const FtraceMetadata& metadata) {
  auto trace_packet = writer_->NewTracePacket();
  auto inode_file_map = trace_packet->set_inode_file_map();
  // TODO(azappone): Get mount_points & add to the proto
  auto inodes = metadata.inodes;
  for (const auto& inode : inodes) {
    uint32_t block_device_id = inode.first;
    uint64_t inode_number = inode.second;
    auto* entry = inode_file_map->add_entries();
    entry->set_inode_number(inode_number);
    auto block_device_map = file_system_inodes_->find(block_device_id);
    if (block_device_map != file_system_inodes_->end()) {
      auto inode_map = block_device_map->second.find(inode_number);
      if (inode_map != block_device_map->second.end()) {
        entry->set_type(inode_map->second.first);
        for (const auto& path : inode_map->second.second)
          entry->add_paths(path.c_str());
      }
    }
  }
  trace_packet->Finalize();
}

void ProbesProducer::InodeFileMapDataSource::OnInodes(
    const std::vector<std::pair<uint64_t, uint32_t>>& inodes) {
  PERFETTO_DLOG("Saw FtraceBundle with %zu inodes.", inodes.size());
}

}  // namespace perfetto
