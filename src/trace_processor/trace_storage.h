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

#ifndef SRC_TRACE_PROCESSOR_TRACE_STORAGE_H_
#define SRC_TRACE_PROCESSOR_TRACE_STORAGE_H_

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace perfetto {
namespace trace_processor {

// Stores a data inside a trace file in a columnar form. This makes it efficient
// to read or search across a single field of the trace (e.g. all the thread
// names for a given CPU).
class TraceStorage {
 public:
  virtual ~TraceStorage();

  // Adds a sched slice for a given cpu.
  // Virtual for testing.
  virtual void InsertSchedSwitch(uint32_t cpu,
                                 uint64_t timestamp,
                                 uint32_t prev_pid,
                                 uint32_t prev_state,
                                 const char* prev_comm,
                                 uint64_t prev_comm_len,
                                 uint32_t next_pid);

  // Reading methods.
  std::deque<uint64_t>* start_timestamps_for_cpu(uint32_t cpu) {
    if (cpu_events_.size() <= cpu ||
        cpu_events_[cpu].cpu_ == std::numeric_limits<uint32_t>::max()) {
      return nullptr;
    }
    return &cpu_events_[cpu].start_timestamps;
  }

 private:
  // Each StringId is an offset into |strings_|.
  using StringId = size_t;
  using StringHash = uint32_t;

  struct SlicesPerCpu {
    uint32_t cpu_ = std::numeric_limits<uint32_t>::max();

    // Each vector below has the same number of entries (the number of slices
    // in the trace for the CPU).
    std::deque<uint64_t> start_timestamps;
    std::deque<uint64_t> durations;
    std::deque<StringId> thread_names;
  };

  struct SchedSwitchEvent {
    uint64_t cpu = 0;
    uint64_t timestamp = 0;
    uint32_t prev_pid = 0;
    uint32_t prev_state = 0;
    StringId prev_comm_id;
    uint32_t next_pid = 0;
    bool valid = false;
  };

  void AddSliceForCpu(uint32_t cpu,
                      uint64_t start_timestamp,
                      uint64_t duration,
                      StringId thread_name_id);

  // Return an unqiue identifier for the contents of each string.
  StringId InternString(const char* data, uint64_t length);

  // One entry for each CPU in the trace.
  std::vector<SchedSwitchEvent> last_sched_per_cpu_;

  // One entry for each CPU in the trace.
  std::vector<SlicesPerCpu> cpu_events_;

  // One entry for each unique string in the trace.
  std::deque<std::string> strings_;

  // One entry for each unique string in the trace.
  std::unordered_map<StringHash, StringId> string_pool_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TRACE_STORAGE_H_
