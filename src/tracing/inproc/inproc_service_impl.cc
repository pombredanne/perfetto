/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/tracing/inproc/inproc_service_impl.h"

#include "perfetto/base/task_runner.h"
// #include "perfetto/tracing/core/data_source_config.h"
// #include "perfetto/tracing/core/producer.h"
// #include "perfetto/tracing/core/shared_memory_arbiter.h"
// #include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/shared_memory_arbiter.h"
#include "src/tracing/inproc/inproc_producer_impl.h"
#include "src/tracing/inproc/inproc_shared_memory.h"

namespace perfetto {

InprocServiceImpl::InprocServiceImpl(base::TaskRunner* task_runner)
    : task_runner_(task_runner),
      svc_(
          TracingService::CreateInstance(std::unique_ptr<SharedMemory::Factory>(
                                             new InprocSharedMemory::Factory()),
                                         task_runner)) {}

std::unique_ptr<TracingService::ProducerEndpoint>
InprocServiceImpl::ConnectProducer(const std::string& name,
                                   Producer* producer,
                                   base::TaskRunner* producer_task_runner) {
  // This method is called on the Producer task runner, which might be different
  // than the Service task runner.

  std::unique_ptr<InprocProducerImpl> inproc_producer_impl(
      new InprocProducerImpl(producer, producer_task_runner, task_runner_));

  THIS CALL IS ON THE WRONG THREAD.
  std::unique_ptr<TracingService::ProducerEndpoint> real_producer_endpoint =
      svc_->ConnectProducer(inproc_producer_impl.get(), /*uid=*/0, name,
                            /*hint=*/ base::kPageSize);


  inproc_producer_impl->set_real_service(std::move(real_producer_endpoint));

  return inproc_producer_impl;
}

std::unique_ptr<TracingService::ConsumerEndpoint>
InprocServiceImpl::ConnectConsumer(Consumer*, base::TaskRunner*) {
  PERFETTO_CHECK(false);  // Not implemented yet.
}

}  // namespace perfetto
