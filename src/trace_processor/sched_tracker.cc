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

#include "src/trace_processor/sched_tracker.h"
#include "perfetto/base/utils.h"
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

SchedTracker::SchedTracker(TraceProcessorContext* context)
    : context_(context){};

SchedTracker::~SchedTracker() = default;

void SchedTracker::PushSchedSwitch(uint32_t cpu,
                                   uint64_t timestamp,
                                   uint32_t prev_pid,
                                   uint32_t prev_state,
                                   base::StringView prev_comm,
                                   uint32_t next_pid) {
  // At this stage all events should be globally timestamp ordered.
  PERFETTO_DCHECK(prev_timestamp_ <= timestamp);
  prev_timestamp_ = timestamp;
  PERFETTO_DCHECK(cpu < base::kMaxCpus);
  SchedSwitchEvent* prev = &last_sched_per_cpu_[cpu];
  // If we had a valid previous event, then inform the storage about the
  // slice.
  if (prev->valid() && prev->next_pid != 0 /* Idle process (swapper/N) */) {
    uint64_t duration = timestamp - prev->timestamp;
    StringId prev_thread_name_id = context_->storage->InternString(prev_comm);
    UniqueTid utid = context_->process_tracker->UpdateThread(
        prev->timestamp, prev->next_pid /* == prev_pid */, prev_thread_name_id);
    uint64_t cycles = CalculateCycles(cpu, prev->timestamp, timestamp);
    context_->storage->AddSliceToCpu(cpu, prev->timestamp, duration, utid,
                                     cycles);
  }

  // If the this events previous pid does not match the previous event's next
  // pid, make a note of this.
  if (prev_pid != prev->next_pid) {
    context_->storage->AddMismatchedSchedSwitch();
  }

  // Update the map with the current event.
  prev->timestamp = timestamp;
  prev->prev_pid = prev_pid;
  prev->prev_state = prev_state;
  prev->next_pid = next_pid;
};

uint64_t SchedTracker::CalculateCycles(uint32_t cpu,
                                       uint64_t start_ns,
                                       uint64_t end_ns) {
  auto frequencies = context_->storage->GetFreqForCpu(cpu);
  if (frequencies.empty())
    return 0;

  long double cycles = 0;
  uint64_t time_last_processed = start_ns;
  uint64_t prev_freq = 0;

  // Find the index of the first pair with a timestamp >= to start_ns.
  while (lower_index < frequencies.size() &&
         frequencies[lower_index].first < start_ns) {
    ++lower_index;
  };

  // Find the index of the first pair with timestamp > end_ns.
  size_t upper_index = lower_index;
  while (upper_index < frequencies.size() &&
         frequencies[upper_index].first <= end_ns) {
    ++upper_index;
  };

  if (lower_index != 0) {
    prev_freq = frequencies[lower_index - 1].second;
  }

  // For each frequency change within |start_ns| and |end_ns| multiply the
  // prev frequency by the time that has passed.
  for (size_t i = lower_index; i != upper_index; ++i) {
    cycles += ((frequencies[i].first - time_last_processed) / 1E9L) * prev_freq;
    prev_freq = frequencies[i].second;
    time_last_processed = frequencies[i].first;
  }
  cycles += ((end_ns - time_last_processed) / 1E9L) * prev_freq;
  return static_cast<uint64_t>(std::round(cycles));
}

}  // namespace trace_processor
}  // namespace perfetto
