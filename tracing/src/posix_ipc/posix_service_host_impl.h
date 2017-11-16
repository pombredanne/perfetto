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

#ifndef TRACING_SRC_POSIX_IPC_POSIX_SERVICE_HOST_IMPL_H_
#define TRACING_SRC_POSIX_IPC_POSIX_SERVICE_HOST_IMPL_H_

#include <memory>

#include "tracing/posix_ipc/posix_service_host.h"

namespace perfetto {
namespace ipc {
class Host;
}

class PosixServiceHostImpl : public PosixServiceHost {
 public:
  PosixServiceHostImpl(base::TaskRunner*);
  ~PosixServiceHostImpl() override;

  // PosixServiceHost implementation.
  bool Start(const char* producer_socket_name) override;
  Service* service_for_testing() const override;
  void set_observer_for_testing(ObserverForTesting*) override;

 private:
  PosixServiceHostImpl(const PosixServiceHostImpl&) = delete;
  PosixServiceHostImpl& operator=(const PosixServiceHostImpl&) = delete;

  base::TaskRunner* const task_runner_;
  ObserverForTesting* observer_ = nullptr;

  std::unique_ptr<Service> svc_;

  std::unique_ptr<ipc::Host> producer_ipc_host_;
};

}  // namespace perfetto

#endif  // TRACING_SRC_POSIX_IPC_POSIX_SERVICE_HOST_IMPL_H_
