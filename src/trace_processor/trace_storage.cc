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

TraceStorage::TraceStorage() {
  // Upid/utid 0 is reserved for invalid processes/threads.
  unique_processes_.emplace_back(0);
  unique_threads_.emplace_back(0);

  // Reserve string ID 0 for the empty string.
  InternString("");

  // Initialize all CPUs
  for (size_t cpu = 0; cpu < base::kMaxCpus; cpu++) {
    InitalizeCounterValue(static_cast<int64_t>(cpu), CounterType::CPU_ID);
  }
}

TraceStorage::~TraceStorage() {}

void TraceStorage::AddSliceToCpu(uint32_t cpu,
                                 uint64_t start_ns,
                                 uint64_t duration_ns,
                                 UniqueTid utid,
                                 uint64_t cycles) {
  cpu_events_[cpu].AddSlice(start_ns, duration_ns, utid, cycles);
};

StringId TraceStorage::InternString(base::StringView str) {
  auto hash = str.Hash();
  auto id_it = string_index_.find(hash);
  if (id_it != string_index_.end()) {
    PERFETTO_DCHECK(base::StringView(string_pool_[id_it->second]) == str);
    return id_it->second;
  }
  string_pool_.emplace_back(str.ToStdString());
  StringId string_id = string_pool_.size() - 1;
  string_index_.emplace(hash, string_id);
  return string_id;
}

void TraceStorage::PushCounterValue(uint64_t timestamp,
                                    uint32_t value,
                                    uint32_t ref,
                                    CounterType type) {
  CounterContext context = {ref, type};
  auto& vals = counters_[context];
  if (!(vals.size() == 0) && timestamp < vals.timestamps.back()) {
    PERFETTO_ELOG("counter out of order by %.4f ms, skipping",
                  (vals.timestamps.back() - timestamp) / 1e6);
    return;
  }
  vals.timestamps.emplace_back(timestamp);
  vals.values.emplace_back(value);
}

void TraceStorage::InitalizeCounterValue(int64_t ref, CounterType type) {
  counters_[{ref, type}];
}

void TraceStorage::PushCpuFreq(uint64_t timestamp,
                               uint32_t cpu,
                               uint32_t new_freq) {
  PERFETTO_DCHECK(cpu <= base::kMaxCpus);
  auto& vals = counters_[{cpu, CounterType::CPU_ID}];
  vals.counter_name_id = InternString("CpuFreq");
  PushCounterValue(timestamp, new_freq, cpu, CounterType::CPU_ID);
}

void TraceStorage::ResetStorage() {
  *this = TraceStorage();
}

}  // namespace trace_processor
}  // namespace perfetto
