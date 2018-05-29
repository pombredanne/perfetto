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

#include "src/trace_processor/columnar_trace.h"

namespace perfetto {
namespace trace_processor {

void ColumnarTrace::AddSliceForCpu(uint32_t cpu,
                                   uint64_t start_timestamp,
                                   uint64_t duration,
                                   const char* thread_name) {
  SlicesPerCpu* slices = &cpu_events_[cpu];
  slices->cpu_ = cpu;
  slices->start_timestamps.emplace_back(start_timestamp);
  slices->durations.emplace_back(duration);

  uint32_t hash = 0;
  for (size_t i = 0; i < strlen(thread_name); ++i) {
    hash = static_cast<uint32_t>(thread_name[i]) + (hash * 31);
  }
  if (string_pool_.find(hash) == string_pool_.end()) {
    string_pool_.emplace(hash, std::string(thread_name));
  }
  slices->thread_names.emplace_back(hash);
}

}  // namespace trace_processor
}  // namespace perfetto
