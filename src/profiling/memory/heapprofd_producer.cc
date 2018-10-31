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

#include "src/profiling/memory/heapprofd_producer.h"

#include <inttypes.h>

#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/trace_writer.h"

namespace perfetto {
namespace profiling {
namespace {
constexpr char kHeapprofdDataSource[] = "android.heapprofd";
}

HeapprofdProducer::HeapprofdProducer(base::TaskRunner* task_runner,
                                     TracingService::ProducerEndpoint* endpoint)
    : task_runner_(task_runner), endpoint_(endpoint) {}

HeapprofdProducer::~HeapprofdProducer() = default;

void HeapprofdProducer::OnConnect() {
  DataSourceDescriptor desc;
  desc.set_name(kHeapprofdDataSource);
  endpoint_->RegisterDataSource(desc);
}

void HeapprofdProducer::OnDisconnect() {}
void HeapprofdProducer::SetupDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig& cfg) {
  if (cfg.name() != kHeapprofdDataSource)
    return;

  auto buffer_id = static_cast<BufferID>(cfg.target_buffer());
  auto trace_writer = endpoint_->CreateTraceWriter(buffer_id);

  bool inserted;
  std::tie(std::ignore, inserted) = data_sources_.emplace(id, cfg);
  if (!inserted)
    PERFETTO_DFATAL("Received duplicated data source instance id: %" PRIu64,
                    id);
}

void HeapprofdProducer::StartDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig&) {
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    PERFETTO_DFATAL("Received invalid data source instance to start: %" PRIu64,
                    id);
    return;
  }
  DataSource& data_source = it->second;
  data_source.Start(task_runner_);
}

void HeapprofdProducer::StopDataSource(DataSourceInstanceID id) {
  if (data_sources_.erase(id) != 1)
    PERFETTO_DFATAL("Trying to stop non existing data source: %" PRIu64, id);
}

void HeapprofdProducer::OnTracingSetup() {}
void HeapprofdProducer::Flush(FlushRequestID flush_id,
                              const DataSourceInstanceID* data_source_ids,
                              size_t num_data_sources) {
  for (size_t i = 0; i < num_data_sources; ++i) {
    DataSourceInstanceID id = data_source_ids[i];
    auto it = data_sources_.find(id);
    if (it == data_sources_.end()) {
      PERFETTO_DFATAL(
          "Received invalid data source instance to start: %" PRIu64, id);
      return;
    }
    DataSource& data_source = it->second;
    data_source.Flush();
  }
  endpoint_->NotifyFlushComplete(flush_id);
}

}  // namespace profiling
}  // namespace perfetto
