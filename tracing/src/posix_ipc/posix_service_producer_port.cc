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

#include "tracing/src/posix_ipc/posix_service_host_impl.h"

#include <inttypes.h>

#include "base/logging.h"
#include "base/task_runner.h"
#include "ipc/host.h"
#include "tracing/core/data_source_config.h"
#include "tracing/core/data_source_descriptor.h"
#include "tracing/core/service.h"
#include "tracing/src/posix_ipc/posix_service_producer_port.h"

namespace perfetto {

PosixServiceProducerPort::PosixServiceProducerPort(Service* core_service)
    : core_service_(core_service), weak_ptr_factory_(this) {}

// TODO think to destruction order of this and the ipc::Host, what happens to
// callbacks?
PosixServiceProducerPort::~PosixServiceProducerPort() = default;

PosixServiceProducerPort::ProducerProxy*
PosixServiceProducerPort::GetProducerForCurrentRequest() {
  const ipc::ClientID ipc_client_id = ipc::Service::client_info().client_id();
  PERFETTO_CHECK(ipc_client_id);
  auto it = producers_.find(ipc_client_id);
  if (it == producers_.end()) {
    // Create a new entry.
    std::unique_ptr<ProducerProxy> proxy(new ProducerProxy());
    proxy->service_endpoint = core_service_->ConnectProducer(proxy.get());
    it = producers_.emplace(ipc_client_id, std::move(proxy)).first;
  }
  return it->second.get();
}

void PosixServiceProducerPort::RegisterDataSource(
    const RegisterDataSourceRequest& req,
    DeferredRegisterDataSourceResponse response) {
  ProducerProxy& producer_proxy = *GetProducerForCurrentRequest();
  const std::string data_source_name = req.data_source_descriptor().name();
  if (producer_proxy.pending_data_sources.count(data_source_name)) {
    PERFETTO_DLOG(
        "A RegisterDataSource() request for \"%s\" is already pending",
        data_source_name.c_str());
    return response.Reject();
  }

  // Deserialize IPC proto -> core DataSourceDescriptor.
  DataSourceDescriptor dsd;
  dsd.name = data_source_name;
  producer_proxy.pending_data_sources[data_source_name] = std::move(response);
  auto weak_this = weak_ptr_factory_.GetWeakPtr();

  // TODO: add test to cover the case of IPC going away before the
  // RegisterDataSource callback is received.
  const ipc::ClientID ipc_client_id = ipc::Service::client_info().client_id();
  GetProducerForCurrentRequest()->service_endpoint->RegisterDataSource(
      dsd, [weak_this, ipc_client_id, data_source_name](DataSourceID id) {
        if (!weak_this)
          return;
        weak_this->OnDataSourceRegistered(ipc_client_id, data_source_name, id);
      });
}

void PosixServiceProducerPort::OnDataSourceRegistered(
    ipc::ClientID ipc_client_id,
    std::string data_source_name,
    DataSourceID id) {
  auto producer_it = producers_.find(ipc_client_id);
  if (producer_it == producers_.end())
    return;  // The producer died in the meantime.
  ProducerProxy& producer_proxy = *producer_it->second;

  auto it = producer_proxy.pending_data_sources.find(data_source_name);
  PERFETTO_CHECK(it != producer_proxy.pending_data_sources.end());

  PERFETTO_DLOG("Data source %s registered, Client:%" PRIu64 " ID: %" PRIu64,
                data_source_name.c_str(), ipc_client_id, id);

  DeferredRegisterDataSourceResponse ipc_repsonse = std::move(it->second);
  producer_proxy.pending_data_sources.erase(it);
  auto res = ipc::AsyncResult<RegisterDataSourceResponse>::Create();
  res->set_data_source_id(id);
  ipc_repsonse.Resolve(std::move(res));
}

void PosixServiceProducerPort::OnClientDisconnected() {
  ipc::ClientID client_id = ipc::Service::client_info().client_id();
  PERFETTO_DLOG("Client %" PRIu64 " disconnected", client_id);
  producers_.erase(client_id);
}

// TODO: test what happens if we receive the following tasks, in order:
// RegisterDataSource, UnregisterDataSource, OnDataSourceRegistered.
// which essentially means that the client posted back to back a
// ReqisterDataSource and UnregisterDataSource speculating on the next id.
void PosixServiceProducerPort::UnregisterDataSource(
    const UnregisterDataSourceRequest& req,
    DeferredUnregisterDataSourceResponse response) {
  ProducerProxy& producer_proxy = *GetProducerForCurrentRequest();
  producer_proxy.service_endpoint->UnregisterDataSource(req.data_source_id());

  // UnregisterDataSource doesn't expect any meaningful response.
  response.Resolve(ipc::AsyncResult<UnregisterDataSourceResponse>::Create());
}

void PosixServiceProducerPort::DrainSharedBuffer(
    const DrainSharedBufferRequest& req,
    DeferredDrainSharedBufferResponse response) {
  ProducerProxy& producer_proxy = *GetProducerForCurrentRequest();
  std::vector<uint32_t> changed_pages;
  for (const uint32_t& changed_page : req.changed_pages())
    changed_pages.push_back(changed_page);
  producer_proxy.service_endpoint->DrainSharedBuffer(changed_pages);
  response.Resolve(ipc::AsyncResult<DrainSharedBufferResponse>::Create());
}

void PosixServiceProducerPort::GetAsyncCommand(
    const GetAsyncCommandRequest&,
    DeferredGetAsyncCommandResponse response) {
  // Keep the back channel to send async commands to the Producer open through
  // all the lifetime of the ProducerProxy. We'll use this to trigger commands
  // on the Producer such as CreateDataSourceInstance().
  ProducerProxy& producer_proxy = *GetProducerForCurrentRequest();
  producer_proxy.async_producer_commands = std::move(response);
}

PosixServiceProducerPort::ProducerProxy::ProducerProxy() = default;
PosixServiceProducerPort::ProducerProxy::~ProducerProxy() = default;
void PosixServiceProducerPort::ProducerProxy::OnConnect(ProducerID,
                                                        SharedMemory*) {}
void PosixServiceProducerPort::ProducerProxy::OnDisconnect() {}
void PosixServiceProducerPort::ProducerProxy::CreateDataSourceInstance(
    DataSourceInstanceID,
    const DataSourceConfig&) {}
void PosixServiceProducerPort::ProducerProxy::TearDownDataSourceInstance(
    DataSourceInstanceID) {}

}  // namespace perfetto
