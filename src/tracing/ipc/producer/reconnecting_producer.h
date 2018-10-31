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

#ifndef SRC_TRACING_IPC_PRODUCER_RECONNECTING_PRODUCER_H_
#define SRC_TRACING_IPC_PRODUCER_RECONNECTING_PRODUCER_H_

#include "perfetto/base/logging.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/ipc/producer_ipc_client.h"

namespace perfetto {

class ReconnectingProducer : public Producer {
 public:
  ReconnectingProducer(const char* producer_name,
                       const char* socket_name,
                       base::TaskRunner* task_runner,
                       std::function<std::unique_ptr<Producer>(
                           TracingService::ProducerEndpoint*)> factory);

  void OnConnect() override;
  void OnDisconnect() override;
  void SetupDataSource(DataSourceInstanceID id,
                       const DataSourceConfig& cfg) override;
  void StartDataSource(DataSourceInstanceID id,
                       const DataSourceConfig& cfg) override;
  void StopDataSource(DataSourceInstanceID id) override;
  void OnTracingSetup() override;
  void Flush(FlushRequestID id,
             const DataSourceInstanceID* data_source_ids,
             size_t num_data_sources) override;

  void ConnectWithRetries();

 private:
  enum State {
    kNotStarted = 0,
    kNotConnected,
    kConnecting,
    kConnected,
  };

  void Connect();
  void ResetConnectionBackoff();
  void IncreaseConnectionBackoff();

  const char* producer_name_;
  const char* socket_name_;
  base::TaskRunner* const task_runner_;
  std::function<std::unique_ptr<Producer>(TracingService::ProducerEndpoint*)>
      factory_;
  std::unique_ptr<Producer> producer_;

  uint32_t connection_backoff_ms_ = 0;
  State state_ = kNotStarted;
  std::unique_ptr<TracingService::ProducerEndpoint> endpoint_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_IPC_PRODUCER_RECONNECTING_PRODUCER_H_
