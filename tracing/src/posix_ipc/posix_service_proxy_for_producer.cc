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

#include "base/task_runner.h"
#include "ipc/client.h"
#include "tracing/core/data_source_config.h"
#include "tracing/core/data_source_descriptor.h"
#include "tracing/core/producer.h"
#include "tracing/src/posix_ipc/posix_shared_memory.h"
#include "tracing/src/posix_ipc/tracing_service_producer_port.ipc.h"

// TODO think to what happens when PosixServiceProxyForProducer gets destroyed
// w.r.t. the Producer pointer. Also think to lifetime of the Producer* during
// the callbacks.

namespace perfetto {
namespace {

struct PerProcessIPCContext {
  static PerProcessIPCContext& GetInstance();
  TracingServiceProducerPortProxy* NewProducer(const char* service_socket_name);
  void OnProducerDestroyed();

  // Owns the IPC client socket.
  std::unique_ptr<ipc::Client> ipc_client;

  // The interface that allows to make IPC calls on the remote Service.
  std::unique_ptr<TracingServiceProducerPortProxy> service_proxy;

  int num_producers = 0;
};

// static
PerProcessIPCContext& PerProcessIPCContext::GetInstance() {
  PerProcessIPCContext* instance = new PerProcessIPCContext();
  return *instance;
}

TracingServiceProducerPortProxy* PerProcessIPCContext::NewProducer(
    const char* service_socket_name) {
  if (num_producers++ > 0)
    return;
  PERFETTO_DCHECK(!ipc_client);
  PERFETTO_DCHECK(!service_proxy);
  ipc_client = ipc::Client::CreateInstance(service_socket_name);
  service_proxy.reset(new TracingServiceProducerPortProxy());
  ipc_client.BindService(service_proxy.GetWeakPtr());
  return service_proxy.get();
}

void PerProcessIPCContext::OnProducerDestroyed() {
  PERFETTO_DCHECK(PerProcessIPCContext > 0);
  if (--num_producers > 0)
    return;
  service_proxy.reset();
  ipc_client.reset();
}

}  // namespace.

PosixServiceProxyForProducer::PosixServiceProxyForProducer(
    Producer* producer,
    TaskRunner* task_runner)
    : producer_(producer), task_runner_(task_runner), ipc_(this) {}

PosixServiceProxyForProducer::~PosixServiceProxyForProducer() {
  if (remote_service_)
    OnProducerDestroyed();
}

void PosixServiceProxyForProducer::Connect(const char* service_socket_name) {
  remote_service_ =
      PerProcessIPCContext::GetInstance().NewProducer(service_socket_name);
  PERFETTO_DCHECK(remote_service_);
}

}  // namespace perfetto
