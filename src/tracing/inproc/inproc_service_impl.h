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

#ifndef SRC_TRACING_INPROC_INPROC_SERVICE_IMPL_H_
#define SRC_TRACING_INPROC_INPROC_SERVICE_IMPL_H_

#include <memory>
#include <string>

#include "perfetto/base/task_runner.h"
#include "perfetto/base/weak_ptr.h"
#include "perfetto/tracing/core/tracing_service.h"
#include "perfetto/tracing/inproc/inproc_service.h"

namespace perfetto {

class InprocService;
class Producer;
class SharedMemoryArbiter;

class InprocServiceImpl : public InprocService {
 public:
  InprocServiceImpl(base::TaskRunner*);

  // InprocService implementation.
  virtual std::unique_ptr<TracingService::ProducerEndpoint> ConnectProducer(
      const std::string& name,
      Producer*,
      base::TaskRunner*) override;

  virtual std::unique_ptr<TracingService::ConsumerEndpoint> ConnectConsumer(
      Consumer*,
      base::TaskRunner*) override;

 private:
  base::TaskRunner* const task_runner_;
  std::unique_ptr<TracingService> svc_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_INPROC_INPROC_SERVICE_IMPL_H_
