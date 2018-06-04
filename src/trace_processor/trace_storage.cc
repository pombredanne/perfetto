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

void TraceStorage::AddSliceForCpu(uint32_t cpu,
                                  uint64_t start_timestamp,
                                  uint64_t duration,
                                  StringId thread_name_id) {
  if (cpu_events_.size() <= cpu)
    cpu_events_.resize(cpu + 1);

  SlicesPerCpu* slices = &cpu_events_[cpu];
  slices->cpu_ = cpu;
  slices->start_timestamps.emplace_back(start_timestamp);
  slices->durations.emplace_back(duration);
  slices->thread_names.emplace_back(thread_name_id);
}

void TraceStorage::AddProcessEntry(uint64_t pid,
                                   uint64_t time_start,
                                   StringId process_name_id) {
  // Store a new upid for that pid.
  auto pids_it = pids_.find(pid);
  if (pids_it == pids_.end()) {
    std::deque<UniquePid> upids(1, current_upid_);
    pids_.emplace(pid, upids);
  } else {
    // If pid has been seen before, set the end time of the previous entry
    // to the start time of this entry.
    UniquePid prev_upid = pids_it->second.back();
    process_entries_[prev_upid].time_end = time_start;
    pids_it->second.emplace_back(current_upid_);
  }
  current_upid_++;

  // Make a new process entry for that upid.
  ProcessEntry new_process;
  new_process.time_start = time_start;
  new_process.process_name = process_name_id;
  process_entries_.emplace_back(new_process);
}

std::deque<TraceStorage::UniquePid>* TraceStorage::UpidsForPid(uint64_t pid) {
  auto pid_it = pids_.find(pid);
  if (pid_it != pids_.end()) {
    return &pid_it->second;
  }
  return nullptr;
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

}  // namespace trace_processor
}  // namespace perfetto
