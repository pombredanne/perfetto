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

#include "src/trace_processor/trace_storage.h"

#include <string.h>

namespace perfetto {
namespace trace_processor {

TraceStorage::~TraceStorage() {}

void TraceStorage::InsertSchedSwitch(uint32_t cpu,
                                     uint64_t timestamp,
                                     uint32_t prev_pid,
                                     uint32_t prev_state,
                                     const char* prev_comm,
                                     size_t prev_comm_len,
                                     uint32_t next_pid) {
  if (last_sched_per_cpu_.size() <= cpu)
    last_sched_per_cpu_.resize(cpu + 1);

  SchedSwitchEvent* event = &last_sched_per_cpu_[cpu];

  // If we had a valid previous event, then inform the storage about the
  // slice.
  if (event->valid) {
    AddSliceForCpu(cpu, event->timestamp, timestamp - event->timestamp,
                   event->prev_comm_id);
  }

  // Update the map with the current event.
  event->cpu = cpu;
  event->timestamp = timestamp;
  event->prev_pid = prev_pid;
  event->prev_state = prev_state;
  event->prev_comm_id = InternString(prev_comm, prev_comm_len);
  event->next_pid = next_pid;
  event->valid = true;
}

void TraceStorage::AddSliceForCpu(uint32_t cpu,
                                  uint64_t start_timestamp,
                                  uint64_t duration,
                                  StringId thread_name_id) {
  if (cpu_events_.size() <= cpu)
    cpu_events_.resize(cpu + 1);

  SlicesPerCpu* slices = &cpu_events_[cpu];
  slices->Setup(cpu);
  slices->AddSlice(start_timestamp, duration, thread_name_id);
}

TraceStorage::StringId TraceStorage::InternString(const char* data,
                                                  uint64_t length) {
  uint32_t hash = 0;
  for (uint64_t i = 0; i < length; ++i) {
    hash = static_cast<uint32_t>(data[i]) + (hash * 31);
  }
  auto id_it = string_pool_.find(hash);
  if (id_it != string_pool_.end()) {
    return id_it->second;
  }
  strings_.emplace_back(data, length);
  string_pool_.emplace(hash, strings_.size() - 1);
  return strings_.size() - 1;
}

void TraceStorage::SlicesPerCpu::Setup(uint32_t cpu) {
  cpu_ = cpu;
  valid_ = true;
}

void TraceStorage::SlicesPerCpu::AddSlice(uint64_t start_timestamp,
                                          uint64_t duration,
                                          StringId thread_name_id) {
  start_timestamps_.emplace_back(start_timestamp);
  durations_.emplace_back(duration);
  thread_names_.emplace_back(thread_name_id);
}

}  // namespace trace_processor
}  // namespace perfetto
