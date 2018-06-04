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
  // Each StringId is an offset into |strings_|.
  typedef size_t StringId;
  // UniquePid is an offset into |process_entries_|.
  typedef size_t UniquePid;

  // Information about a unique process seen in a trace.
  struct ProcessEntry {
    uint64_t time_start;
    uint64_t time_end;
    StringId process_name;
  };

  // Adds a sched slice for a given cpu.
  void AddSliceForCpu(uint32_t cpu,
                      uint64_t start_timestamp,
                      uint64_t duration,
                      StringId thread_name_id);

  // Return an unqiue identifier for the contents of each string.
  StringId InternString(const char* data, uint64_t length);

  // Adds a process entry for a given pid.
  void AddProcessEntry(uint64_t pid,
                       uint64_t time_start,
                       StringId process_name_id);

  // Finds the upids for a given pid. Returns a nullptr if none are found.
  std::deque<UniquePid>* UpidsForPid(uint64_t pid);

  // Reading methods.
  std::deque<uint64_t>* start_timestamps_for_cpu(uint32_t cpu) {
    if (cpu_events_.size() <= cpu ||
        cpu_events_[cpu].cpu_ == std::numeric_limits<uint32_t>::max()) {
      return nullptr;
    }
    return &cpu_events_[cpu].start_timestamps;
  }

  const ProcessEntry* process_for_upid(UniquePid upid) {
    if (process_entries_.size() <= upid) {
      return nullptr;
    }
    return &process_entries_[upid];
  }

  const std::string* string_for_string_id(StringId id) {
    if (strings_.size() <= id) {
      return nullptr;
    }
    return &strings_[id];
  }

 private:
  typedef uint32_t StringHash;
  UniquePid current_upid_ = 0;

  struct SlicesPerCpu {
    uint32_t cpu_ = std::numeric_limits<uint32_t>::max();

    // Each deque below has the same number of entries (the number of slices
    // in the trace for the CPU).
    std::deque<uint64_t> start_timestamps;
    std::deque<uint64_t> durations;
    std::deque<StringId> thread_names;
  };

  // One entry for each CPU in the trace.
  std::vector<SlicesPerCpu> cpu_events_;

  // One entry for each unique string in the trace.
  std::deque<std::string> strings_;

  // One entry for each unique string in the trace.
  std::unordered_map<StringHash, StringId> string_pool_;

  // Each pid can have multiple UniquePid entries, a new UniquePid is assigned
  // each time a process is seen in the trace.
  std::unordered_map<uint64_t, std::deque<UniquePid>> pids_;

  // One entry for each UniquePid.
  std::deque<ProcessEntry> process_entries_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TRACE_STORAGE_H_
