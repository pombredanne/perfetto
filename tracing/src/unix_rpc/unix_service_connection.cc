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

#include "tracing/unix_rpc/unix_service_connection.h"

#include "tracing/core/service.h"
#include "tracing/src/unix_rpc/unix_service_proxy_for_producer.h"

namespace perfetto {

// static
std::unique_ptr<Service::ProducerEndpoint>
UnixServiceConnection::ConnectAsProducer(const char* service_socket_name,
                                         Producer* producer,
                                         TaskRunner* task_runner) {
  std::unique_ptr<UnixServiceProxyForProducer> service_proxy(
      new UnixServiceProxyForProducer(producer, task_runner));
  if (!service_proxy->Connect(service_socket_name))
    return nullptr;
  return std::move(service_proxy);
}

}  // namespace perfetto
