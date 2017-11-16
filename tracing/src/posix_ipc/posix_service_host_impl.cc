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

#include "base/logging.h"
#include "base/task_runner.h"
#include "ipc/host.h"
#include "tracing/core/service.h"
#include "tracing/src/posix_ipc/posix_service_producer_port.h"

namespace perfetto {

// Implements the publicly exposed factory method declared in
// include/tracing/posix_ipc/posix_service_host.h.

// static
std::unique_ptr<PosixServiceHost> PosixServiceHost::CreateInstance(
    base::TaskRunner* task_runner) {
  return std::unique_ptr<PosixServiceHost>(
      new PosixServiceHostImpl(task_runner));
}

PosixServiceHostImpl::PosixServiceHostImpl(base::TaskRunner* task_runner)
    : task_runner_(task_runner) {}

PosixServiceHostImpl::~PosixServiceHostImpl() {}

bool PosixServiceHostImpl::Start(const char* producer_socket_name) {
  PERFETTO_DCHECK(!svc_);  // Check if already started.

  // Create and initialize the platform-independent tracing business logic.
  std::unique_ptr<SharedMemory::Factory> shm_factory;
  // new PosixSharedMemory::Factory());
  svc_ = Service::CreateInstance(std::move(shm_factory), task_runner_);

  // Initialize the IPC transport.
  producer_ipc_host_ =
      ipc::Host::CreateInstance(producer_socket_name, task_runner_);
  if (!producer_ipc_host_)
    return false;

  bool producer_service_exposed = producer_ipc_host_->ExposeService(
      std::unique_ptr<ipc::Service>(new PosixServiceProducerPort(svc_.get())));
  PERFETTO_CHECK(producer_service_exposed);
  return true;
}

Service* PosixServiceHostImpl::service_for_testing() const {
  return svc_.get();
}

void PosixServiceHostImpl::set_observer_for_testing(ObserverForTesting* obs) {
  observer_ = obs;
}

}  // namespace perfetto
