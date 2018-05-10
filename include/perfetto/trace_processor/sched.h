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

#include <functional>

namespace perfetto {

namespace base {
class TaskRunner;
}

namespace protos {
class Query;
class SchedEvent;
}  // namespace protos

namespace trace_processor {

class BlobReader;

class Sched {
 public:
  explicit Sched(base::TaskRunner*);

  using GetSchedEventsCallback =
      std::function<void(const protos::SchedEvent*, size_t)>;
  void GetSchedEvents(const protos::Query&, GetSchedEventsCallback);

  void GetQuantizedSchedActivity(const protos::Query&);

 private:
  Sched(const Sched&) = delete;
  Sched& operator=(const Sched&) = delete;

  base::TaskRunner* const task_runner_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_SCHED_H_
