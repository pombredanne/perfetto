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

#include <map>
#include <memory>
#include <utility>

#include "perfetto/base/task_runner.h"
#include "perfetto/ftrace_reader/ftrace_controller.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "perfetto/tracing/ipc/producer_ipc_client.h"

#include "perfetto/trace/filesystem/inode_file_map.pbzero.h"

#ifndef SRC_TRACED_PROBES_PROBES_PRODUCER_H_
#define SRC_TRACED_PROBES_PROBES_PRODUCER_H_

namespace perfetto {

class ProbesProducer : public Producer {
 public:
  ProbesProducer();
  ~ProbesProducer() override;

  // Producer Impl:
  void OnConnect() override;
  void OnDisconnect() override;
  void CreateDataSourceInstance(DataSourceInstanceID,
                                const DataSourceConfig&) override;
  void TearDownDataSourceInstance(DataSourceInstanceID) override;

  // Our Impl
  void ConnectWithRetries(const char* socket_name,
                          base::TaskRunner* task_runner);
  void CreateFtraceDataSourceInstance(DataSourceInstanceID id,
                                      const DataSourceConfig& source_config);
  void CreateProcessStatsDataSourceInstance(
      const DataSourceConfig& source_config);
  void CreateInodeFileMapDataSourceInstance(
      DataSourceInstanceID id,
      const DataSourceConfig& source_config);

  void OnMetadata(const FtraceMetadata& metadata);

 private:
  using FtraceBundleHandle =
      protozero::MessageHandle<protos::pbzero::FtraceEventBundle>;
  using Type = protos::pbzero::InodeFileMap_Entry_Type;
  using InodeDataMap =
      std::map<uint64_t,
               std::pair<protos::pbzero::InodeFileMap_Entry_Type,
                         std::set<std::string>>>;
  using InodeFileMap = perfetto::protos::pbzero::InodeFileMap;

  class SinkDelegate : public FtraceSink::Delegate {
   public:
    explicit SinkDelegate(base::TaskRunner* task_runner,
                          std::unique_ptr<TraceWriter> writer);
    ~SinkDelegate() override;

    // FtraceDelegateImpl
    FtraceBundleHandle GetBundleForCpu(size_t cpu) override;
    void OnBundleComplete(size_t cpu,
                          FtraceBundleHandle bundle,
                          const FtraceMetadata& metadata) override;

    void sink(std::unique_ptr<FtraceSink> sink) { sink_ = std::move(sink); }
    void OnInodes(const std::vector<uint64_t>& inodes);

   private:
    base::TaskRunner* task_runner_;
    std::unique_ptr<FtraceSink> sink_ = nullptr;
    std::unique_ptr<TraceWriter> writer_;

    // Keep this after the TraceWriter because TracePackets must not outlive
    // their originating writer.
    TraceWriter::TracePacketHandle trace_packet_;
    // Keep this last.
    base::WeakPtrFactory<SinkDelegate> weak_factory_;
  };

  class InodeFileMapDataSource {
   public:
    explicit InodeFileMapDataSource(
        std::map<uint64_t, InodeDataMap>* file_system_inodes,
        std::unique_ptr<TraceWriter> writer);
    ~InodeFileMapDataSource();

    void WriteInodes(const FtraceMetadata& metadata);
    bool AddInodeFileMapEntry(
        InodeFileMap* inode_file_map,
        uint64_t block_device_id,
        uint64_t inode,
        const std::map<uint64_t, InodeDataMap>& block_device_map);

   private:
    std::map<uint64_t, InodeDataMap>* file_system_inodes_;
    std::unique_ptr<TraceWriter> writer_;
  };

  enum State {
    kNotStarted = 0,
    kNotConnected,
    kConnecting,
    kConnected,
  };

  void Connect();
  void ResetConnectionBackoff();
  void IncreaseConnectionBackoff();
  void AddWatchdogsTimer(DataSourceInstanceID id,
                         const DataSourceConfig& source_config);

  // Fills in block_device_map of device id to inode data.
  // If the block_device_map already has entries, will return immediately.
  // Accepts a map of inode_number to block_device_id. If given non-empty map,
  // only adds to the map for the provided unresolved inodes found in the
  // root directory. If given an empty map, adds to the block_device_map for
  // every file found in the root directory.
  static void FillDeviceToInodeDataMap(
      const std::string& root_directory,
      std::map<uint64_t, InodeDataMap>* block_device_map,
      const std::map<uint64_t, uint64_t>& unresolved_inodes);

  State state_ = kNotStarted;
  base::TaskRunner* task_runner_;
  std::unique_ptr<Service::ProducerEndpoint> endpoint_ = nullptr;
  std::unique_ptr<FtraceController> ftrace_ = nullptr;
  bool ftrace_creation_failed_ = false;
  uint64_t connection_backoff_ms_ = 0;
  const char* socket_name_ = nullptr;
  // Keeps track of id for each type of data source.
  std::map<DataSourceInstanceID, std::string> instances_;
  std::map<DataSourceInstanceID, std::unique_ptr<SinkDelegate>> delegates_;
  std::map<DataSourceInstanceID, base::Watchdog::Timer> watchdogs_;
  std::map<DataSourceInstanceID, std::unique_ptr<InodeFileMapDataSource>>
      file_map_sources_;
  std::map<uint64_t, InodeDataMap> system_inodes_;
};
}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_PROBES_PRODUCER_H_
