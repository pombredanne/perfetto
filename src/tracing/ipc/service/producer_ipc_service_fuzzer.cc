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

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/utils.h"
#include "perfetto/ipc/host.h"
#include "perfetto/tracing/ipc/service_ipc_host.h"
#include "src/base/test/test_task_runner.h"

class ServiceDelegate : public ThreadDelegate {
 public:
  ServiceDelegate() = default;
  ~ServiceDelegate() override = default;

  void Initialize(base::TaskRunner* task_runner) override {
    svc_ = ServiceIPCHost::CreateInstance(task_runner);
    unlink(TEST_PRODUCER_SOCK_NAME);
    unlink(TEST_CONSUMER_SOCK_NAME);
    svc_->Start(TEST_PRODUCER_SOCK_NAME, TEST_CONSUMER_SOCK_NAME);
  }

 private:
  std::unique_ptr<ServiceIPCHost> svc_;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  perfetto::base::TestTaskRunner task_runner;

  int producer_fds[2];
  PERFETTO_CHECK(pipe(producer_fds) == 0);
  perfetto::base::ScopedFile host_producer_fd(producer_fds[0]);
  perfetto::base::ScopedFile client_producer_fd(producer_fds[1]);

  int consumer_fds[2];
  PERFETTO_CHECK(pipe(consumer_fds) == 0);
  perfetto::base::ScopedFile host_consumer_fd(consumer_fds[0]);
  perfetto::base::ScopedFile client_consumer_fd(consumer_fds[1]);

  auto producer_ipc = perfetto::ServiceIPCHost::CreateInstance(&task_runner);
  producer_ipc->Start(std::move(host_producer_fd), std::move(host_consumer_fd));
  return 0;
}
