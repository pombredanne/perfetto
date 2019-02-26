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

#ifndef INCLUDE_PERFETTO_TRACING_INPROC_INPROC_SERVICE_H_
#define INCLUDE_PERFETTO_TRACING_INPROC_INPROC_SERVICE_H_

#include <memory>

#include "perfetto/tracing/core/tracing_service.h"

namespace perfetto {
namespace base {
class TaskRunner;
}  // namespace base.

class TracingService;

// Creates an instance of the service for fully in-process use.
class InprocService {
 public:
  static std::unique_ptr<InprocService> CreateInstance(base::TaskRunner*);
  virtual ~InprocService();
  InprocService(const InprocService&) = delete;
  InprocService& operator=(const InprocService&) = delete;

  virtual std::unique_ptr<TracingService::ProducerEndpoint>
  ConnectProducer(const std::string& name, Producer*, base::TaskRunner*);

  virtual std::unique_ptr<TracingService::ConsumerEndpoint> ConnectConsumer(
      Consumer*,
      base::TaskRunner*);

 protected:
  InprocService();
};

}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACING_INPROC_INPROC_SERVICE_H_
