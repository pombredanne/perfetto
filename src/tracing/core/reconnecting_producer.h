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

#ifndef SRC_TRACING_CORE_RECONNECTING_PRODUCER_H_
#define SRC_TRACING_CORE_RECONNECTING_PRODUCER_H_

#include "perfetto/base/logging.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/ipc/producer_ipc_client.h"

namespace perfetto {

template <typename T>
class ReconnectingProducer : public Producer {
 public:
  ReconnectingProducer(const char* socket_name, base::TaskRunner* task_runner)
      : socket_name_(socket_name), task_runner_(task_runner) {}

  void OnConnect() override {
    PERFETTO_DCHECK(state_ == kConnecting);
    state_ = kConnected;
    ResetConnectionBackoff();
    producer_.reset(new T(task_runner_));
    producer_->OnConnect();
  }

  void OnDisconnect() override {
    PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
    PERFETTO_LOG("Disconnected from tracing service");
    if (state_ == kConnected) {
      producer_.reset(nullptr);
      return task_runner_->PostTask([this] { ConnectWithRetries(); });
    }

    state_ = kNotConnected;
    IncreaseConnectionBackoff();
    task_runner_->PostDelayedTask([this] { Connect(); },
                                  connection_backoff_ms_);
  }

  void SetupDataSource(DataSourceInstanceID id,
                       const DataSourceConfig& cfg) override {
    producer_->SetupDataSource(id, cfg);
  }

  void StartDataSource(DataSourceInstanceID id,
                       const DataSourceConfig& cfg) override {
    producer_->StartDataSource(id, cfg);
  }

  void StopDataSource(DataSourceInstanceID id) override {
    producer_->StopDataSource(id);
  }

  void OnTracingSetup() override { producer_->OnTracingSetup(); }

  void Flush(FlushRequestID id,
             const DataSourceInstanceID* data_source_ids,
             size_t num_data_sources) override {
    producer_->Flush(id, data_source_ids, num_data_sources);
  }

  void ConnectWithRetries() {
    PERFETTO_DCHECK(state_ == kNotStarted);
    state_ = kNotConnected;
    ResetConnectionBackoff();
    Connect();
  }

 private:
  static constexpr uint32_t kInitialConnectionBackoffMs = 100;
  static constexpr uint32_t kMaxConnectionBackoffMs = 30 * 1000;

  enum State {
    kNotStarted = 0,
    kNotConnected,
    kConnecting,
    kConnected,
  };

  void Connect() {
    PERFETTO_DCHECK(state_ == kNotConnected);
    state_ = kConnecting;
    endpoint_ = ProducerIPCClient::Connect(
        socket_name_, this, "perfetto.traced_probes", task_runner_);
  }

  void ResetConnectionBackoff() {
    connection_backoff_ms_ = kInitialConnectionBackoffMs;
  }

  void IncreaseConnectionBackoff() {
    connection_backoff_ms_ *= 2;
    if (connection_backoff_ms_ > kMaxConnectionBackoffMs)
      connection_backoff_ms_ = kMaxConnectionBackoffMs;
  }

  const char* socket_name_;
  std::unique_ptr<T> producer_;

  uint32_t connection_backoff_ms_ = 0;
  State state_ = kNotStarted;
  base::TaskRunner* task_runner_ = nullptr;
  std::unique_ptr<TracingService::ProducerEndpoint> endpoint_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_CORE_RECONNECTING_PRODUCER_H_
