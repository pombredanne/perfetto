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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_TRACE_PROCESSOR_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_TRACE_PROCESSOR_H_

#include <memory>

namespace perfetto {

namespace base {
class TaskRunner;
}

namespace trace_processor {

class BlobReader;
class Sched;

class TraceProcessor {
 public:
  TraceProcessor(base::TaskRunner*, BlobReader*);

  // There is going to be one of these methods for each type of processed proto.
  Sched* sched();

 private:
  TraceProcessor(const TraceProcessor&) = delete;
  TraceProcessor& operator=(const TraceProcessor&) = delete;

  base::TaskRunner* const task_runner_;
  std::unique_ptr<Sched> sched_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_TRACE_PROCESSOR_H_
