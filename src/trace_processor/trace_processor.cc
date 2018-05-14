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

#include "perfetto/trace_processor/trace_processor.h"

#include "perfetto/base/task_runner.h"
#include "perfetto/trace_processor/sched.h"

namespace perfetto {
namespace trace_processor {

TraceProcessor::TraceProcessor(base::TaskRunner* task_runner,
                               BlobReader* reader)
    : task_runner_(task_runner), reader_(reader) {}

Sched* TraceProcessor::sched() {
  if (!sched_)
    sched_.reset(new Sched(task_runner_, reader_));
  return sched_.get();
}

}  // namespace trace_processor
}  // namespace perfetto
