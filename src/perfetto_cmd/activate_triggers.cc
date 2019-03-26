/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/perfetto_cmd/activate_triggers.h"

#include <memory>

#include "perfetto/base/logging.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/ipc/producer_ipc_client.h"
#include "src/tracing/ipc/default_socket.h"

namespace perfetto {

class DataSourceConfig;

ActivateTriggersProducer::ActivateTriggersProducer(
    bool* success,
    PlatformTaskRunner* task_runner,
    const std::vector<std::string>* const triggers)
    : success_(success),
      task_runner_(task_runner),
      triggers_(triggers),
      producer_endpoint_(ProducerIPCClient::Connect(GetProducerSocket(),
                                                    this,
                                                    "perfetto_cmd_producer",
                                                    task_runner)) {
  // Give the socket up to 1 minute to attach and send the triggers before
  // reporting a failure.
  task_runner_->PostDelayedTask([this]() { task_runner_->Quit(); }, 60000);
}

ActivateTriggersProducer::~ActivateTriggersProducer() {}

void ActivateTriggersProducer::OnConnect() {
  PERFETTO_DLOG("Connected as a producer and sending triggers.");
  // Send activation signal.
  producer_endpoint_->ActivateTriggers(*triggers_);
  *success_ = true;
  task_runner_->Quit();
}

void ActivateTriggersProducer::OnDisconnect() {
  PERFETTO_DLOG("Disconnected as a producer.");
}

void ActivateTriggersProducer::OnTracingSetup() {
  PERFETTO_DFATAL("Attempted to OnTracingSetup() on commandline producer");
}
void ActivateTriggersProducer::SetupDataSource(DataSourceInstanceID,
                                               const DataSourceConfig&) {
  PERFETTO_DFATAL("Attempted to SetupDataSource() on commandline producer");
}
void ActivateTriggersProducer::StartDataSource(DataSourceInstanceID,
                                               const DataSourceConfig&) {
  PERFETTO_DFATAL("Attempted to StartDataSource() on commandline producer");
}
void ActivateTriggersProducer::StopDataSource(DataSourceInstanceID) {
  PERFETTO_DFATAL("Attempted to StopDataSource() on commandline producer");
}
void ActivateTriggersProducer::Flush(FlushRequestID,
                                     const DataSourceInstanceID*,
                                     size_t) {
  PERFETTO_DFATAL("Attempted to Flush() on commandline producer");
}

std::unique_ptr<ActivateTriggersProducer> ActivateTriggers(
    const std::vector<std::string>& triggers,
    PlatformTaskRunner* task_runner,
    bool* success) {
  PERFETTO_DCHECK(!triggers.empty());
  PERFETTO_DCHECK(task_runner);
  return std::unique_ptr<ActivateTriggersProducer>(
      new ActivateTriggersProducer(success, task_runner, &triggers));
}
}  // namespace perfetto
