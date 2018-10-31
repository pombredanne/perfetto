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

#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"

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
  const HeapprofdConfig& heapprofd_cfg = cfg.heapprofd_config();
}

void HeapprofdProducer::StartDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig& cfg) {}

void HeapprofdProducer::StopDataSource(DataSourceInstanceID id) {}
void HeapprofdProducer::OnTracingSetup() {}
void HeapprofdProducer::Flush(FlushRequestID id,
                              const DataSourceInstanceID* data_source_ids,
                              size_t num_data_sources) {}
}  // namespace profiling
}  // namespace perfetto
