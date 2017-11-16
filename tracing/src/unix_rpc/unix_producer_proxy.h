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

#ifndef TRACING_SRC_POSIX_IPC_POSIX_PRODUCER_PROXY_H_
#define TRACING_SRC_POSIX_IPC_POSIX_PRODUCER_PROXY_H_

#include "tracing/core/basic_types.h"
#include "tracing/core/producer.h"
#include "tracing/core/service.h"
#include "tracing/src/unix_rpc/unix_socket.h"
#include "tracing/unix_rpc/unix_service_host.h"

namespace perfetto {

class Service;
class TaskRunner;
class UnixSharedMemory;

// Exposed to the ServiceImpl business logic. Pretends to be a Producer, all it
// does is forwarding requests back to the remote Producer and proxies the calls
// back to the Service's ProducerEndpoint.
class UnixProducerProxy : public Producer {
 public:
  UnixProducerProxy(UnixSocket,
                    TaskRunner*,
                    UnixServiceHost::ObserverForTesting*);
  ~UnixProducerProxy() override;

  // void SendSharedMemory(const UnixSharedMemory&);
  UnixSocket* conn() { return &conn_; }
  void set_service(Service::ProducerEndpoint* svc) { svc_ = svc; }

  // Producer implementation.
  void OnConnect(ProducerID, SharedMemory*) override;
  void CreateDataSourceInstance(DataSourceInstanceID,
                                const DataSourceConfig&) override;
  void TearDownDataSourceInstance(DataSourceInstanceID) override;

 private:
  UnixProducerProxy(const UnixProducerProxy&) = delete;
  UnixProducerProxy& operator=(const UnixProducerProxy&) = delete;

  void OnDataAvailable();

  UnixSocket conn_;
  TaskRunner* const task_runner_;
  UnixServiceHost::ObserverForTesting* const observer_;
  Service::ProducerEndpoint* svc_ = nullptr;
};

}  // namespace perfetto

#endif  // TRACING_SRC_POSIX_IPC_POSIX_PRODUCER_PROXY_H_
\
