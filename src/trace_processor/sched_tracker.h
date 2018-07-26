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

#ifndef SRC_TRACE_PROCESSOR_SCHED_TRACKER_H_
#define SRC_TRACE_PROCESSOR_SCHED_TRACKER_H_

#include <array>
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class SchedTracker {
 public:
  SchedTracker(TraceProcessorContext*);

  struct SchedSwitchEvent {
    uint32_t cpu = 0;
    uint64_t timestamp = 0;
    uint32_t prev_pid = 0;
    uint32_t prev_state = 0;
    TraceStorage::StringId prev_thread_name_id = 0;
    uint32_t next_pid = 0;

    bool valid() const { return timestamp != 0; }
  };

  void PushSchedSwitch(uint32_t cpu,
                       uint64_t timestamp,
                       uint32_t prev_pid,
                       uint32_t prev_state,
                       const char* prev_comm,
                       size_t prev_comm_len,
                       uint32_t next_pid);

 private:
  // One entry for each CPU in the trace.
  std::array<SchedSwitchEvent, TraceStorage::kMaxCpus> last_sched_per_cpu_;

  TraceProcessorContext* context_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_SCHED_TRACKER_H_
