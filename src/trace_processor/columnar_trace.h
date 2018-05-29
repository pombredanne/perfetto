/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_COLUMNAR_TRACE_H_
#define SRC_TRACE_PROCESSOR_COLUMNAR_TRACE_H_

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace perfetto {
namespace trace_processor {

// Stores a data inside a trace file in a columnar form. This makes it efficient
// to read or search across a single field of the trace (e.g. all the thread
// names for a given CPU).
class ColumnarTrace {
 public:
  // Adds a sched slice for a given cpu.
  void AddSliceForCpu(uint32_t cpu,
                      uint64_t start_timestamp,
                      uint64_t duration,
                      const char* thread_name);

  // Reading methods.
  const std::vector<uint64_t>& start_timestamps_for_cpu(uint32_t cpu) {
    return cpu_events_[cpu].start_timestamps;
  }

 private:
  typedef uint32_t StringId;

  struct SlicesPerCpu {
    uint32_t cpu_ = 0;

    // Each vector below has the same number of entries (the number of slices
    // in the trace for the CPU).
    std::vector<uint64_t> start_timestamps;
    std::vector<uint64_t> durations;
    std::vector<StringId> thread_names;
  };

  // One entry for each CPU in the trace.
  std::unordered_map<uint32_t, SlicesPerCpu> cpu_events_;

  // One entry for each unique string in the trace.
  std::unordered_map<StringId, std::string> string_pool_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_COLUMNAR_TRACE_H_
