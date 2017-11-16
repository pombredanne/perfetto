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

#ifndef TRACING_SRC_POSIX_IPC_POSIX_SERVICE_PROXY_FOR_PRODUCER_H_
#define TRACING_SRC_POSIX_IPC_POSIX_SERVICE_PROXY_FOR_PRODUCER_H_

#include <stdint.h>

#include <vector>

#include "tracing/core/basic_types.h"
#include "tracing/core/service.h"

namespace perfetto {
class Producer;
class TaskRunner;
class PosixSharedMemory;

// Implements the Service::ProducerEndpoint exposed to Producer. Proxies the
// requests to a remote Service onto an IPC channel.
class PosixServiceProxyForProducer : public Service::ProducerEndpoint,
                                     public ipc::ServiceProxy::EventListener {
 public:
  PosixServiceProxyForProducer(Producer*, TaskRunner*);
  ~PosixServiceProxyForProducer() override;

  void Connect(const char* service_socket_name);

  // Service::ProducerEndpoint implementation.
  void RegisterDataSource(const DataSourceDescriptor&,
                          RegisterDataSourceCallback) override;
  void UnregisterDataSource(DataSourceID) override;
  void DrainSharedBuffer(const std::vector<uint32_t>& changed_pages) override;

  // ipc::ServiceProxy::EventListener implementation.
  void OnConnect() override;
  void OnDisconnect() override;

 private:
  // TODO think to destruction order.
  Producer* const producer_;
  TaskRunner* const task_runner_;
  RegisterDataSourceCallback pending_register_data_source_callback_;
  std::unique_ptr<PosixSharedMemory> shared_memory_;
  TracingServiceProducerPortProxy* remote_service_ = nullptr;
};

}  // namespace perfetto

#endif  // TRACING_SRC_POSIX_IPC_POSIX_SERVICE_PROXY_FOR_PRODUCER_H_
