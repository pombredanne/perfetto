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

#include "tracing/core/basic_types.h"
#include "tracing/core/service.h"
#include "tracing/src/unix_rpc/unix_socket.h"

namespace perfetto {

class Producer;
class TaskRunner;
class UnixSharedMemory;

// Implements the Service::ProducerEndpoint interface doing RPC over a UNIX
// socket.
class UnixServiceProxyForProducer : public Service::ProducerEndpoint {
 public:
  UnixServiceProxyForProducer(Producer*, TaskRunner*);
  ~UnixServiceProxyForProducer() override;

  bool Connect(const char* service_socket_name);

  // ServiceProxyForProducer implementation.
  ProducerID GetID() const override;
  void RegisterDataSource(const DataSourceDescriptor&,
                          RegisterDataSourceCallback) override;
  void UnregisterDataSource(DataSourceID) override;
  void NotifyPageAcquired(uint32_t page_index) override;
  void NotifyPageReleased(uint32_t page_index) override;

 private:
  void OnDataAvailable();

  ProducerID id_ = 0;
  Producer* const producer_;
  TaskRunner* const task_runner_;
  RegisterDataSourceCallback pending_register_data_source_callback_;
  UnixSocket conn_;
  std::unique_ptr<UnixSharedMemory> shared_memory_;
};

}  // namespace perfetto

#endif  // TRACING_SRC_POSIX_IPC_POSIX_SERVICE_PROXY_FOR_PRODUCER_H_
