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

#ifndef SRC_PERFETTO_CMD_ACTIVATE_TRIGGERS_H_
#define SRC_PERFETTO_CMD_ACTIVATE_TRIGGERS_H_

#include <string>
#include <vector>

#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/tracing_service.h"
#include "src/perfetto_cmd/platform_task_runner.h"

namespace perfetto {

class DataSourceConfig;

// This is a producer that only sends the provided |triggers| to the service. It
// will never register any data sources.
class ActivateTriggersProducer : public Producer {
 public:
  ActivateTriggersProducer(bool* success,
                           PlatformTaskRunner* task_runner,
                           const std::vector<std::string>* const triggers);
  ~ActivateTriggersProducer() override;

  // We will call ActivateTriggers() on the |producer_endpoint_| and then
  // immediately call Quit() on |task_runner|.
  void OnConnect() override;
  // We have no clean up to do OnDisconnect.
  void OnDisconnect() override;

  // Unimplemented methods are below this.
  void OnTracingSetup() override;
  void SetupDataSource(DataSourceInstanceID, const DataSourceConfig&) override;
  void StartDataSource(DataSourceInstanceID, const DataSourceConfig&) override;
  void StopDataSource(DataSourceInstanceID) override;
  void Flush(FlushRequestID, const DataSourceInstanceID*, size_t) override;

 private:
  bool* success_;
  PlatformTaskRunner* task_runner_;
  const std::vector<std::string>* const triggers_;
  std::unique_ptr<TracingService::ProducerEndpoint> producer_endpoint_;
};

// Creates a ActivateTriggersProducer, which once the async connections call is
// complete will set |success| to true. This function does not block. You must
// wait on |task_runner->Run()| to safely access |success|.
std::unique_ptr<ActivateTriggersProducer> ActivateTriggers(
    const std::vector<std::string>& triggers,
    PlatformTaskRunner* task_runner,
    bool* success);
}  // namespace perfetto

#endif  // SRC_PERFETTO_CMD_ACTIVATE_TRIGGERS_H_
