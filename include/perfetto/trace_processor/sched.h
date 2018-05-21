/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_SCHED_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_SCHED_H_

#include <stdint.h>

#include <functional>
#include <memory>

namespace perfetto {

namespace base {
class TaskRunner;
}

namespace protos {
class Query;
class SchedEvents;
class QuantizedSchedActivity;
}  // namespace protos

namespace trace_processor {

class BlobReader;

// This implements the RPC methods defined in sched.proto.
class Sched {
 public:
  Sched(base::TaskRunner*, BlobReader*);

  using GetSchedEventsCallback =
      std::function<void(const protos::SchedEvents&)>;
  void GetSchedEvents(const protos::Query&, GetSchedEventsCallback);

  using GetQuantizedSchedActivityCallback =
      std::function<void(const protos::QuantizedSchedActivity&)>;
  void GetQuantizedSchedActivity(const protos::Query&,
                                 GetQuantizedSchedActivityCallback);

 private:
  Sched(const Sched&) = delete;
  Sched& operator=(const Sched&) = delete;
  void DoRead();

  uint32_t bytes_read_ = 0;
  base::TaskRunner* const task_runner_;
  BlobReader* const reader_;
  std::unique_ptr<uint8_t[]> buf_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_SCHED_H_
