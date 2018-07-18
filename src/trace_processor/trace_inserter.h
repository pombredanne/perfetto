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
  virtual ~TraceInserter();

  void ResetStorage() { storage_ = {}; }

  // Adds a sched slice for a given cpu.
  // Virtual for testing.
  virtual void PushSchedSwitch(uint32_t cpu,
                               uint64_t timestamp,
                               uint32_t prev_pid,
                               const char* prev_comm,
                               size_t prev_comm_len,
                               uint32_t next_pid);

  virtual void PushProcess(uint64_t timestamp,
                           uint32_t pid,
                           const char* process_name,
                           size_t process_name_len);

  virtual void PushThread(uint64_t timestamp, uint32_t tid, uint32_t tgid);

 private:
  struct Event {
    virtual ~Event();
    virtual void StoreEvent(TraceStorage*) = 0;
    uint64_t timestamp = 0;
  };

  struct SchedSwitchEvent : Event {
    ~SchedSwitchEvent() override = default;
    bool valid() const { return timestamp != 0; }
    void StoreEvent(TraceStorage*) override;
    uint64_t cpu = 0;
    uint32_t tid = 0;
    uint64_t duration = 0;
    TraceStorage::StringId thread_name_id = 0;
    uint32_t next_tid = 0;
  };

  struct ProcessEvent : Event {
    ~ProcessEvent() override = default;
    void StoreEvent(TraceStorage*) override;
    uint32_t pid;
    const char* process_name;
    size_t process_name_len;
  };

  struct ThreadEvent : Event {
    ~ThreadEvent() override = default;
    void StoreEvent(TraceStorage*) override;
    uint32_t tid;
    uint32_t tgid;
  };

  void SetWindow(uint64_t value) { window = value; }

  TraceStorage storage_;

  uint64_t window = 0;  // 1 sec
  uint64_t first_timestamp = 0;
  uint64_t latest_timestamp = 0;

  // In order to store the duration of an event, we don't store it in
  // |sched_events_| until we see the next event. This stores the last event
  // for each cpu temporarily.
  std::array<SchedSwitchEvent, TraceStorage::kMaxCpus> last_sched_event_;

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
