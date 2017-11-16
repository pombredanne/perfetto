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

#include <functional>
#include <vector>

#include "ipc/service_proxy.h"
#include "tracing/core/basic_types.h"
#include "tracing/core/service.h"
#include "tracing/src/posix_ipc/tracing_service_producer_port.ipc.h"

namespace perfetto {

namespace base {
class TaskRunner;
}  // namespace base

class Producer;
class PosixSharedMemory;

// Implements the Service::ProducerEndpoint exposed to Producer. Proxies the
// requests to a remote Service onto an IPC channel.
class PosixServiceProxyForProducer : public Service::ProducerEndpoint,
                                     public ipc::ServiceProxy::EventListener {
 public:
  PosixServiceProxyForProducer(Producer*, base::TaskRunner*);
  ~PosixServiceProxyForProducer() override;

  // Service::ProducerEndpoint implementation.
  void RegisterDataSource(const DataSourceDescriptor&,
                          RegisterDataSourceCallback) override;
  void UnregisterDataSource(DataSourceID) override;
  void DrainSharedBuffer(const std::vector<uint32_t>& changed_pages) override;

  // ipc::ServiceProxy::EventListener implementation.
  void OnConnect() override;
  void OnDisconnect() override;

  void set_on_connect(std::function<void(bool /*connected*/)> callback) {
    on_connect_ = callback;
  }

  TracingServiceProducerPortProxy* ipc_endpoint() { return &ipc_endpoint_; }

 private:
  void OnServiceRequest(const GetAsyncCommandResponse&);

  // TODO think to destruction order.
  Producer* const producer_;
  base::TaskRunner* const task_runner_;
  TracingServiceProducerPortProxy ipc_endpoint_;
  RegisterDataSourceCallback pending_register_data_source_callback_;
  std::unique_ptr<PosixSharedMemory> shared_memory_;
  std::function<void(bool /* connected */)> on_connect_;
  bool connected_ = false;
};

}  // namespace perfetto

#endif  // TRACING_SRC_POSIX_IPC_POSIX_SERVICE_PROXY_FOR_PRODUCER_H_
