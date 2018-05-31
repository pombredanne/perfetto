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

#include "src/trace_processor/trace_storage_inserter.h"

#include <string.h>

namespace perfetto {
namespace trace_processor {

TraceStorageInserter::TraceStorageInserter(TraceStorage* trace)
    : trace_(trace) {}

void TraceStorageInserter::InsertSchedSwitch(uint32_t cpu,
                                             uint64_t timestamp,
                                             uint32_t prev_pid,
                                             uint32_t prev_state,
                                             const char* prev_comm,
                                             size_t prev_comm_length,
                                             uint32_t next_pid) {
  if (last_sched_per_cpu_.size() <= cpu)
    last_sched_per_cpu_.resize(cpu + 1);

  SchedSwitchEvent* event = &last_sched_per_cpu_[cpu];

  // We move this to prevent string copies from happening.
  SchedSwitchEvent prev = std::move(*event);

  // If we had a valid previous event, then inform the storage about the
  // slice.
  if (prev.valid) {
    trace_->AddSliceForCpu(cpu, prev.timestamp, timestamp - prev.timestamp,
                           std::move(prev.prev_comm));
  }

  // Update the map with the current event.
  event->cpu = cpu;
  event->timestamp = timestamp;
  event->prev_pid = prev_pid;
  event->prev_state = prev_state;
  event->prev_comm = std::string(prev_comm, prev_comm_length);
  event->next_pid = next_pid;
  event->valid = true;
}

}  // namespace trace_processor
}  // namespace perfetto
