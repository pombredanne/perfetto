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
#include "src/trace_processor/trace_inserter.h"

namespace perfetto {
namespace trace_processor {

TraceInserter::TraceInserter(){};

void TraceInserter::PushSchedSwitch(uint32_t cpu,
                                    uint64_t timestamp,
                                    uint32_t prev_pid,
                                    const char* prev_comm,
                                    size_t prev_comm_len,
                                    uint32_t next_pid) {
  SchedSwitchEvent* prev = &sched_events_[cpu].back();

  // If we had a valid previous event, then inform the storage about the
  // slice.
  if (prev->valid() && prev->next_tid != 0 /* Idle process (swapper/N) */) {
    prev->duration = timestamp - prev->timestamp;

    SchedSwitchEvent current;
    current.timestamp = timestamp;
    current.cpu = cpu;
    current.thread_name_id = storage_->InternString(prev_comm, prev_comm_len);
    current.tid = prev_pid;
    current.next_tid = next_pid;

    // Store event in the trace inserter until it is flushed.
    sched_events_[cpu].emplace_back(std::move(current));
  }

  // If the this events previous pid does not match the previous event's next
  // pid, make a note of this.
  if (prev_pid != prev->next_tid) {
    storage_->AddToMismatchedSchedSwitches(1);
  }

  CheckWindow(timestamp);
}

void TraceInserter::PushProcess(uint64_t timestamp,
                                uint32_t pid,
                                const char* process_name,
                                size_t process_name_len) {
  ProcessEvent p;
  p.pid = pid;
  p.process_name = process_name;
  p.process_name_len = process_name_len;

  // Store event in the trace inserter until it is flushed.
  processes_.emplace_back(std::move(p));
  CheckWindow(timestamp);
}

void TraceInserter::PushThread(uint64_t timestamp,
                               uint32_t tid,
                               uint32_t tgid) {
  ThreadEvent t;
  t.tid = tid;
  t.tgid = tgid;

  // Store event in the trace inserter until it is flushed.
  threads_.emplace_back(std::move(t));
  CheckWindow(timestamp);
}

void TraceInserter::CheckWindow(uint64_t timestamp) {
  if (first_timestamp == 0)
    first_timestamp = timestamp;

  latest_timestamp = timestamp;

  if (latest_timestamp - first_timestamp >= window) {
    FlushEvents();
    first_timestamp = 0;
  }
}

void TraceInserter::FlushEvents() {
  // This will use a priority queue to order the events and store the
  // data in the TraceStorage.
  // Will implement in the next CL.
}

}  // namespace trace_processor
}  // namespace perfetto
