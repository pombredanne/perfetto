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

#ifndef SRC_TRACE_PROCESSOR_TRACE_INSERTER_H_
#define SRC_TRACE_PROCESSOR_TRACE_INSERTER_H_

#include "perfetto/base/logging.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class TraceInserter {
 public:
  TraceInserter();

  void ResetStorage() { storage_ = {}; }

  // Adds a sched slice for a given cpu.
  // Virtual for testing.
  virtual void PushSchedSwitch(uint32_t cpu,
                               uint64_t timestamp,
                               uint32_t prev_pid,
                               const char* prev_comm,
                               size_t prev_comm_len,
                               uint32_t next_pid);

  void PushProcess(uint64_t timestamp,
                   uint32_t pid,
                   const char* process_name,
                   size_t process_name_len);

  void PushThread(uint64_t timestamp, uint32_t tid, uint32_t tgid);

 private:
  struct Event {
    virtual ~Event();
    virtual void StoreEvent(TraceStorage*);
    uint64_t timestamp = 0;
  };

  struct SchedSwitchEvent : Event {
    bool valid() const { return timestamp != 0; }
    void StoreEvent(TraceStorage* storage) override {
      storage->AddSliceToCpu(cpu, timestamp, duration, tid, thread_name_id);
    }
    uint64_t cpu = 0;
    uint32_t tid = 0;
    uint64_t duration = 0;
    TraceStorage::StringId thread_name_id = 0;
    uint32_t next_tid = 0;
  };

  struct ProcessEvent : Event {
    void StoreEvent(TraceStorage* storage) override {
      storage->StoreProcess(pid, process_name, process_name_len);
    }
    uint32_t pid;
    const char* process_name;
    size_t process_name_len;
  };

  struct ThreadEvent : Event {
    void StoreEvent(TraceStorage* storage) override {
      storage->MatchThreadToProcess(tid, tgid);
    }
    uint32_t tid;
    uint32_t tgid;
  };

  virtual ~TraceInserter();

  TraceStorage* storage_;

  uint64_t window = 1000000000;  // 1 sec
  uint64_t first_timestamp = 0;
  uint64_t latest_timestamp = 0;

  // One deque for every CPU in the trace.
  std::array<std::deque<SchedSwitchEvent>, TraceStorage::kMaxCpus>
      sched_events_;

  std::deque<ProcessEvent> processes_;

  std::deque<ThreadEvent> threads_;

  // This method checks the last_ts - first_ts and compares it to the window
  // size. It then calls the method to flush events.
  void CheckWindow(uint64_t timestamp);

  // When the last_ts - first_ts > window time this method is called to store
  // the events in trace storage.
  void FlushEvents();
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TRACE_INSERTER_H_
