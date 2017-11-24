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

#include "tracing/src/ipc/service/consumer_ipc_service.h"

#include <inttypes.h>

#include "base/logging.h"
#include "base/task_runner.h"
#include "ipc/host.h"
#include "tracing/core/service.h"
#include "tracing/core/trace_config.h"
#include "tracing/core/trace_packet.h"

namespace perfetto {

ConsumerIPCService::ConsumerIPCService(Service* core_service)
    : core_service_(core_service), weak_ptr_factory_(this) {}

ConsumerIPCService::~ConsumerIPCService() = default;

ConsumerIPCService::RemoteConsumer*
ConsumerIPCService::GetConsumerForCurrentRequest() {
  const ipc::ClientID ipc_client_id = ipc::Service::client_info().client_id();
  PERFETTO_CHECK(ipc_client_id);
  auto it = consumers_.find(ipc_client_id);
  if (it == consumers_.end()) {
    auto* remote_consumer = new RemoteConsumer();
    consumers_[ipc_client_id].reset(remote_consumer);
    remote_consumer->service_endpoint =
        core_service_->ConnectConsumer(remote_consumer);
    return remote_consumer;
  }
  return it->second.get();
}

// Called by the IPC layer.
void ConsumerIPCService::OnClientDisconnected() {
  ipc::ClientID client_id = ipc::Service::client_info().client_id();
  PERFETTO_DLOG("Consumer %" PRIu64 " disconnected", client_id);
  consumers_.erase(client_id);
}

// Called by the IPC layer.
void ConsumerIPCService::StartTracing(const StartTracingRequest& req,
                                      DeferredStartTracingResponse resp) {
  TraceConfig trace_config;
  for (const auto& proto_buf_cfg : req.buffers()) {
    trace_config.buffers.emplace_back();
    trace_config.buffers.back().size_kb = proto_buf_cfg.size_kb();
  }

  for (const auto& proto_ds : req.data_sources()) {
    trace_config.data_sources.emplace_back();
    TraceConfig::DataSource& ds = trace_config.data_sources.back();
    for (const std::string& pnf : proto_ds.producer_name_filter())
      ds.producer_name_filter.emplace_back(pnf);
    ds.config.name = proto_ds.config().name();
    ds.config.target_buffer = proto_ds.config().target_buffer();
    ds.config.trace_category_filters =
        proto_ds.config().trace_category_filters();
  }
  GetConsumerForCurrentRequest()->service_endpoint->StartTracing(trace_config);
  resp.Resolve(ipc::AsyncResult<StartTracingResponse>::Create());
}

void ConsumerIPCService::StopTracing(const StopTracingRequest& req,
                                     DeferredStopTracingResponse resp) {
  RemoteConsumer* remote_consumer = GetConsumerForCurrentRequest();
  remote_consumer->service_endpoint->StopTracing();
  remote_consumer->stop_tracing_response = std::move(resp);
}

////////////////////////////////////////////////////////////////////////////////
// RemoteConsumer methods
////////////////////////////////////////////////////////////////////////////////

ConsumerIPCService::RemoteConsumer::RemoteConsumer() = default;
ConsumerIPCService::RemoteConsumer::~RemoteConsumer() = default;

// Invoked by the |core_service_| business logic after the ConnectConsumer()
// call. There is nothing to do here, we really expected the ConnectConsumer()
// to just work in the local case.
void ConsumerIPCService::RemoteConsumer::OnConnect() {}

// Invoked by the |core_service_| business logic after we destroy the
// |service_endpoint| (in the RemoteConsumer dtor).
void ConsumerIPCService::RemoteConsumer::OnDisconnect() {}

void ConsumerIPCService::RemoteConsumer::OnTraceData(
    const std::vector<TracePacket>& trace_packets) {
  if (!stop_tracing_response.IsBound())
    return;
  auto result = ipc::AsyncResult<StopTracingResponse>::Create();
  for (const TracePacket& trace_packet : trace_packets)
    result->add_trace_packets(trace_packet.start(), trace_packet.size());

  // TODO: |has_more| EOF logic.
  result.set_has_more(true);

  // TODO lifetime: does the IPC layer guarantee that the arguments of the
  // resolved responses are used inline and not kept around? If not, the
  // start() trace_packet pointers will become invalid.
  stop_tracing_response.Resolve(std::move(result));
}

}  // namespace perfetto
