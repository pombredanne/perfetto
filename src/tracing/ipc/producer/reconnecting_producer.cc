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

#include "src/tracing/ipc/producer/reconnecting_producer.h"

#include "perfetto/base/task_runner.h"

namespace perfetto {
ReconnectingProducer::ReconnectingProducer(
    const char* producer_name,
    const char* socket_name,
    base::TaskRunner* task_runner,
    std::function<std::unique_ptr<Producer>(TracingService::ProducerEndpoint*)>
        factory)
    : producer_name_(producer_name),
      socket_name_(socket_name),
      task_runner_(task_runner),
      factory_(std::move(factory)) {}

void ReconnectingProducer::OnConnect() {
  PERFETTO_DCHECK(state_ == kConnecting);
  PERFETTO_DCHECK(endpoint_);
  state_ = kConnected;
  ResetConnectionBackoff();
  producer_ = factory_(endpoint_.get());
  producer_->OnConnect();
}

void ReconnectingProducer::OnDisconnect() {
  PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
  PERFETTO_LOG("Disconnected from tracing service");
  if (state_ == kConnected) {
    producer_.reset(nullptr);
    return task_runner_->PostTask([this] { ConnectWithRetries(); });
  }

  state_ = kNotConnected;
  IncreaseConnectionBackoff();
  task_runner_->PostDelayedTask([this] { Connect(); }, connection_backoff_ms_);
}

void ReconnectingProducer::SetupDataSource(DataSourceInstanceID id,
                                           const DataSourceConfig& cfg) {
  producer_->SetupDataSource(id, cfg);
}

void ReconnectingProducer::StartDataSource(DataSourceInstanceID id,
                                           const DataSourceConfig& cfg) {
  producer_->StartDataSource(id, cfg);
}

void ReconnectingProducer::StopDataSource(DataSourceInstanceID id) {
  producer_->StopDataSource(id);
}

void ReconnectingProducer::OnTracingSetup() {
  producer_->OnTracingSetup();
}

void ReconnectingProducer::Flush(FlushRequestID id,
                                 const DataSourceInstanceID* data_source_ids,
                                 size_t num_data_sources) {
  producer_->Flush(id, data_source_ids, num_data_sources);
}

void ReconnectingProducer::ConnectWithRetries() {
  PERFETTO_DCHECK(state_ == kNotStarted);
  state_ = kNotConnected;
  ResetConnectionBackoff();
  Connect();
}
}  // namespace perfetto
