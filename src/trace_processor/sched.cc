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

#include "perfetto/trace_processor/sched.h"

#include "perfetto/base/task_runner.h"
#include "perfetto/processed_trace/query.pb.h"
#include "perfetto/processed_trace/sched.pb.h"

namespace perfetto {
namespace trace_processor {

Sched::Sched(base::TaskRunner* task_runner) : task_runner_(task_runner) {}

void Sched::GetSchedEvents(const protos::Query&,
                           GetSchedEventsCallback callback) {
  // TODO(primiano): This function should access the underlying storage, execute
  // the right query and return the results back using the |callback|. Right now
  // let's just cheat.
  task_runner_->PostTask([callback] {
    protos::SchedEvent evt;
    callback(&evt, 1);
  });
}

void Sched::GetQuantizedSchedActivity(const protos::Query&) {
  // TODO(primiano): same here.
}

}  // namespace trace_processor
}  // namespace perfetto
