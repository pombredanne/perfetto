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

#ifndef SRC_TRACE_PROCESSOR_TRACE_STORAGE_INSERTER_H_
#define SRC_TRACE_PROCESSOR_TRACE_STORAGE_INSERTER_H_

#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

// Converts raw trace events into an efficient to query format and inserts
// them into an instance of TraceStorage.
class TraceStorageInserter {
 public:
  TraceStorageInserter(TraceStorage* trace);
  virtual ~TraceStorageInserter();

  // Converts a sched switch into a sched slice and inserts into the storage.
  // Virtual for testing.
  virtual void InsertSchedSwitch(uint32_t cpu,
                                 uint64_t timestamp,
                                 uint32_t prev_pid,
                                 uint32_t prev_state,
                                 const char* prev_comm,
                                 uint64_t prev_comm_len,
                                 uint32_t next_pid);

 private:
  struct SchedSwitchEvent {
    uint64_t cpu = 0;
    uint64_t timestamp = 0;
    uint32_t prev_pid = 0;
    uint32_t prev_state = 0;
    TraceStorage::StringId prev_comm_id;
    uint32_t next_pid = 0;
    bool valid = false;
  };

  std::vector<SchedSwitchEvent> last_sched_per_cpu_;
  TraceStorage* const trace_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TRACE_STORAGE_INSERTER_H_
