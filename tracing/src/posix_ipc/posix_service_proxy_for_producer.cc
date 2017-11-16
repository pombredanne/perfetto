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

#include "tracing/src/posix_ipc/posix_service_proxy_for_producer.h"

#include <inttypes.h>
#include <string.h>

#include "base/task_runner.h"
#include "tracing/core/data_source_config.h"
#include "tracing/core/data_source_descriptor.h"
#include "tracing/core/producer.h"
#include "tracing/src/posix_ipc/posix_shared_memory.h"

// TODO think to what happens when PosixServiceProxyForProducer gets destroyed
// w.r.t. the Producer pointer. Also think to lifetime of the Producer* during
// the callbacks.

namespace perfetto {

PosixServiceProxyForProducer::PosixServiceProxyForProducer(
    Producer* producer,
    base::TaskRunner* task_runner)
    : producer_(producer),
      task_runner_(task_runner),
      ipc_endpoint_(this /* event_listener */) {}

PosixServiceProxyForProducer::~PosixServiceProxyForProducer() = default;

void PosixServiceProxyForProducer::OnConnect() {
  connected_ = true;
  if (on_connect_)
    on_connect_(true);
  on_connect_ = nullptr;

  // Create the back channel to receive commands from the Service.
  ipc::Deferred<GetAsyncCommandResponse> async;

  // The IPC layer guarantees that the outstanding callback will be dropped on
  // the floor if ipc_endpoint_ is destructed between the request and the reply.
  // Binding |this| is hence safe w.r.t. callbacks queued after destruction.
  async.Bind([this](ipc::AsyncResult<GetAsyncCommandResponse> resp) {
    if (!resp)
      return;  // The IPC channel was terminated and this was auto-rejected.
    OnServiceRequest(*resp);
  });
  ipc_endpoint_.GetAsyncCommand(GetAsyncCommandRequest(), std::move(async));
}

void PosixServiceProxyForProducer::OnDisconnect() {
  PERFETTO_DLOG("Tracing service connection failure");
  connected_ = false;
  if (on_connect_)
    on_connect_(false);
  on_connect_ = nullptr;
}

void PosixServiceProxyForProducer::OnServiceRequest(
    const GetAsyncCommandResponse& cmd) {
  if (cmd.cmd_case() == GetAsyncCommandResponse::kStartDataSource) {
    const auto& req = cmd.start_data_source();
    const DataSourceInstanceID dsid = req.new_instance_id();
    const proto::DataSourceConfig& proto_cfg = req.config();
    DataSourceConfig cfg;
    cfg.trace_category_filters = proto_cfg.trace_category_filters();
    producer_->CreateDataSourceInstance(dsid, cfg);
    return;
  }

  if (cmd.cmd_case() == GetAsyncCommandResponse::kStopDataSource) {
    const DataSourceInstanceID dsid = cmd.stop_data_source().instance_id();
    producer_->TearDownDataSourceInstance(dsid);
    return;
  }

  PERFETTO_DLOG("Unknown async request %d received from tracing service",
                cmd.cmd_case());
}

void PosixServiceProxyForProducer::RegisterDataSource(
    const DataSourceDescriptor& descriptor,
    RegisterDataSourceCallback callback) {
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot RegisterDataSource(), disconnected from the tracing service");
    return task_runner_->PostTask(std::bind(callback, 0));
  }
  RegisterDataSourceRequest req;
  auto* proto_descriptor = req.mutable_data_source_descriptor();
  proto_descriptor->set_name(descriptor.name);
  ipc::Deferred<RegisterDataSourceResponse> async_response;
  async_response.Bind(
      [callback](ipc::AsyncResult<RegisterDataSourceResponse> response) {
        if (!response)
          return callback(0);
        callback(response->data_source_id());
      });
  ipc_endpoint_.RegisterDataSource(req, std::move(async_response));
}

void PosixServiceProxyForProducer::UnregisterDataSource(DataSourceID id) {
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot UnregisterDataSource(), disconnected from the tracing service");
    return;
  }
  UnregisterDataSourceRequest req;
  req.set_data_source_id(id);
  ipc_endpoint_.UnregisterDataSource(
      req, ipc::Deferred<UnregisterDataSourceResponse>());
}

void PosixServiceProxyForProducer::DrainSharedBuffer(
    const std::vector<uint32_t>& changed_pages) {
  if (!connected_) {
    PERFETTO_DLOG(
        "Cannot UnregisterDataSource(), disconnected from the tracing service");
    return;
  }
  DrainSharedBufferRequest req;
  for (uint32_t changed_page : changed_pages)
    req.add_changed_pages(changed_page);
  ipc_endpoint_.DrainSharedBuffer(req,
                                  ipc::Deferred<DrainSharedBufferResponse>());
}

}  // namespace perfetto
