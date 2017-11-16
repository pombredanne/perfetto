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

#ifndef TRACING_SRC_POSIX_IPC_POSIX_SERVICE_PRODUCER_PORT_H_
#define TRACING_SRC_POSIX_IPC_POSIX_SERVICE_PRODUCER_PORT_H_

#include <map>
#include <memory>
#include <string>

#include "base/weak_ptr.h"
#include "ipc/basic_types.h"
#include "tracing/core/producer.h"
#include "tracing/core/service.h"
#include "tracing/posix_ipc/posix_service_host.h"
#include "tracing/src/posix_ipc/tracing_service_producer_port.ipc.h"

namespace perfetto {
namespace ipc {
class Host;
}

// Implements the Producer port of the PosixServiceHostImpl Service. This class
// proxies requests and responses between the core service logic (|svc_|) and
// the IPC socket (the methods overriddden from TracingServiceProducerPort).
class PosixServiceProducerPort : public TracingServiceProducerPort {
 public:
  using Service = ::perfetto::Service;  // From tracing/core/service.h .
  explicit PosixServiceProducerPort(Service* core_service);
  ~PosixServiceProducerPort() override;
  // TracingServiceProducerIPCPort implementation (from .proto IPC definition).
  void RegisterDataSource(const RegisterDataSourceRequest&,
                          DeferredRegisterDataSourceResponse) override;
  void UnregisterDataSource(const UnregisterDataSourceRequest&,
                            DeferredUnregisterDataSourceResponse) override;
  void DrainSharedBuffer(const DrainSharedBufferRequest&,
                         DeferredDrainSharedBufferResponse) override;
  void GetAsyncCommand(const GetAsyncCommandRequest&,
                       DeferredGetAsyncCommandResponse) override;
  void OnClientDisconnected() override;

 private:
  // Pretends to be a producer to the core Service business logic, but all it
  // does is proxying methods to the IPC layer.
  class ProducerProxy : public Producer {
   public:
    ProducerProxy();
    ~ProducerProxy() override;
    void OnConnect(ProducerID, SharedMemory*) override;
    void OnDisconnect() override;
    void CreateDataSourceInstance(DataSourceInstanceID,
                                  const DataSourceConfig&) override;
    void TearDownDataSourceInstance(DataSourceInstanceID) override;

    // RegisterDataSource requests that haven't been responded yet.
    std::map<std::string, DeferredRegisterDataSourceResponse>
        pending_data_sources;

    // The per-producer interface obtained from the core service business logic
    // through Service::ConnectProducer(this).
    std::unique_ptr<Service::ProducerEndpoint> service_endpoint;

    DeferredGetAsyncCommandResponse async_producer_commands;
  };

  // Returns the ProducerEndpoint in the core business logic that maps the
  // current IPC channel.
  ProducerProxy* GetProducerForCurrentRequest();

  void OnDataSourceRegistered(ipc::ClientID, std::string, DataSourceID);

  Service* const core_service_;
  base::WeakPtrFactory<PosixServiceProducerPort> weak_ptr_factory_;

  std::map<ipc::ClientID, std::unique_ptr<ProducerProxy>> producers_;
};

}  // namespace perfetto

#endif  // TRACING_SRC_POSIX_IPC_POSIX_SERVICE_PRODUCER_PORT_H_
