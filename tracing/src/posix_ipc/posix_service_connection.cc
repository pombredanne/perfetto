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

#include "tracing/posix_ipc/posix_service_connection.h"

#include <array>
#include <functional>

#include "base/logging.h"
#include "base/thread_checker.h"
#include "ipc/client.h"
#include "tracing/core/service.h"
#include "tracing/src/posix_ipc/posix_service_proxy_for_producer.h"
#include "tracing/src/posix_ipc/tracing_service_producer_port.ipc.h"

namespace perfetto {

namespace {

// Returns the one IPC channel to the service for the current process.
ipc::Client& GetIPCClientForCurrentProcess(const char* socket_name,
                                           base::TaskRunner* task_runner) {
  // This method is not thread-safe.
  static base::ThreadChecker* thread_checker = new base::ThreadChecker();
  PERFETTO_DCHECK(thread_checker->CalledOnValidThread());

  // Connectin to two different services from the same process is currently not
  // supported.
  static const char* last_socket_name = nullptr;
  PERFETTO_DCHECK(!last_socket_name || !strcmp(socket_name, last_socket_name));
  last_socket_name = socket_name;

  // TODO: we could probably shutdown the socket (i.e. |instance|) once all
  // Producer instances are destroyed. But right now it has a marginal benefit.
  static ipc::Client* instance = nullptr;
  if (!instance)
    instance = ipc::Client::CreateInstance(socket_name, task_runner).release();
  return *instance;
}

std::array<PosixServiceProxyForProducer*, 100> g_pending_bindings{};

void OnServiceConnected(
    size_t pending_binding_idx,
    PosixServiceConnection::ConnectAsProducerCallback callback,
    bool connected) {
  std::unique_ptr<Service::ProducerEndpoint> service_endpoint(
      g_pending_bindings[pending_binding_idx]);
  g_pending_bindings[pending_binding_idx] = nullptr;
  callback(connected ? std::move(service_endpoint) : nullptr);
}

}  // namespace

// static
void PosixServiceConnection::ConnectAsProducer(
    const char* service_socket_name,
    Producer* producer,
    base::TaskRunner* task_runner,
    ConnectAsProducerCallback callback) {
  PERFETTO_CHECK(callback);

  // It's unlikely that more than a handful connections will be requested
  // back to back before they are connected. Realistically the connection will
  // happen within the next task. If |g_pending_bindings| fills up, very likely
  // there is leak due to the OnConnect() not being called by the IPC layer.
  for (size_t index = 0; index < g_pending_bindings.size(); index++) {
    if (g_pending_bindings[index])
      continue;
    auto* svc_proxy = new PosixServiceProxyForProducer(producer, task_runner);
    g_pending_bindings[index] = svc_proxy;
    svc_proxy->set_on_connect([index, callback](bool connected) {
      OnServiceConnected(index, callback, connected);
    });
    return GetIPCClientForCurrentProcess(service_socket_name, task_runner)
        .BindService(svc_proxy->ipc_endpoint()->GetWeakPtr());
  }
  PERFETTO_CHECK(false);  // Leak!
}

}  // namespace perfetto
