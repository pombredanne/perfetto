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

#ifndef TRACING_INCLUDE_POSIX_IPC_POSIX_SERVICE_HOST_H_
#define TRACING_INCLUDE_POSIX_IPC_POSIX_SERVICE_HOST_H_

#include <memory>

#include "tracing/core/basic_types.h"

namespace perfetto {
namespace base {
class TaskRunner;
}  // namespace base.

class Service;

// Creates an instance of the service (business logic + UNIX socket transport).
// Exposed to:
//   The code in the tracing client that will host the service e.g., traced.
// Implemented in:
//   src/posix_ipc/posix_service_host_impl.cc
class PosixServiceHost {
 public:
  static std::unique_ptr<PosixServiceHost> CreateInstance(base::TaskRunner*);
  virtual ~PosixServiceHost() {}

  // Start listening on the Producer & Consumer ports. Returns false in case of
  // failure (e.g., something else is listening on |socket_name|).
  virtual bool Start(const char* producer_socket_name) = 0;

  // Testing-only
 public:
  class ObserverForTesting {
   public:
    virtual ~ObserverForTesting() {}
    virtual void OnProducerConnected(ProducerID) = 0;
    virtual void OnDataSourceRegistered(DataSourceID) = 0;
    virtual void OnDataSourceUnregistered(DataSourceID) = 0;
    virtual void OnDataSourceInstanceCreated(DataSourceInstanceID) = 0;
    virtual void OnDataSourceInstanceDestroyed(DataSourceInstanceID) = 0;
  };

  virtual void set_observer_for_testing(ObserverForTesting*) = 0;

  // Accesses the underlying Service business logic. Exposed only for testing.
  virtual Service* service_for_testing() const = 0;
};

}  // namespace perfetto

#endif  // TRACING_INCLUDE_POSIX_IPC_POSIX_SERVICE_HOST_H_
