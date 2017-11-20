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

#include "tracing/ipc/ipc_connection.h"

#include <array>
#include <functional>

#include "base/logging.h"
#include "base/thread_checker.h"
#include "ipc/client.h"
#include "tracing/core/service.h"
#include "tracing/src/ipc/producer/producer_ipc_proxy.h"

#include "tracing/src/ipc/producer_port.ipc.h"  // From producer_port.proto.

namespace perfetto {

namespace {

// Returns the one IPC channel to the service for the current process.
ipc::Client& GetIPCClientForCurrentProcess(const char* socket_name,
                                           base::TaskRunner* task_runner) {
  // This method is not thread-safe.
  static base::ThreadChecker* thread_checker = new base::ThreadChecker();
  PERFETTO_DCHECK(thread_checker->CalledOnValidThread());

  // Connecting to two different services from the same process is currently not
  // supported.
  static const char* last_socket_name = nullptr;
  PERFETTO_DCHECK(!last_socket_name || !strcmp(socket_name, last_socket_name));
  last_socket_name = socket_name;

  // TODO: we could probably shutdown the socket (i.e. destroy |instance|) once
  // all Producers are destroyed. Right now it has a marginal benefit.
  static ipc::Client* instance = nullptr;
  if (!instance)
    instance = ipc::Client::CreateInstance(socket_name, task_runner).release();
  return *instance;
}

}  // namespace

// static
std::unique_ptr<Service::ProducerEndpoint> IPCConnection::ConnectAsProducer(
    const char* service_socket_name,
    Producer* producer,
    base::TaskRunner* task_runner) {
  auto* producer_proxy = new ProducerIPCProxy(producer, task_runner);
  std::unique_ptr<Service::ProducerEndpoint> res(producer_proxy);
  GetIPCClientForCurrentProcess(service_socket_name, task_runner)
      .BindService(producer_proxy->ipc_endpoint()->GetWeakPtr());
  return res;
}

}  // namespace perfetto
