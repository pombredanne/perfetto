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

#ifndef SRC_TRACE_PROCESSOR_SCHED_TRACKER_H_
#define SRC_TRACE_PROCESSOR_SCHED_TRACKER_H_

#include <array>

#include "perfetto/base/string_view.h"
#include "perfetto/base/utils.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class TraceProcessorContext;

// This class takes sched events from the trace and processes them to store
// as sched slices.
class SchedTracker {
 public:
  explicit SchedTracker(TraceProcessorContext*);
  SchedTracker(const SchedTracker&) = delete;
  SchedTracker& operator=(const SchedTracker&) = delete;
  virtual ~SchedTracker();

  StringId GetThreadNameId(uint32_t tid, base::StringView comm);

  // This method is called when a sched switch event is seen in the trace.
  virtual void PushSchedSwitch(uint32_t cpu,
                               uint64_t timestamp,
                               uint32_t prev_pid,
                               uint32_t prev_state,
                               base::StringView prev_comm,
                               uint32_t next_pid);

  // This method is called when a cpu freq event is seen in the trace.
  // TODO(taylori): Move to a more appropriate class or rename class.
  virtual void PushCounter(uint64_t timestamp,
                           double value,
                           StringId name_id,
                           uint64_t ref,
                           RefType ref_type);

 private:
  class PendingSchedSlice {
   public:
    PendingSchedSlice(SchedTracker*,
                      uint64_t timestamp,
                      uint32_t tid,
                      uint32_t cpu);
    ~PendingSchedSlice() = default;
    PendingSchedSlice(PendingSchedSlice&& other) noexcept = default;
    PendingSchedSlice& operator=(PendingSchedSlice&& other) = default;
    PendingSchedSlice(PendingSchedSlice& other) = delete;
    PendingSchedSlice& operator=(PendingSchedSlice& other) = delete;

    void Complete(uint64_t end_timestamp, base::StringView thread_name);
    void PushComplete();
    bool IsComplete() const { return duration_ != 0; }

    uint32_t tid() const { return tid_; }

   private:
    // These are filled in when the class is created.
    SchedTracker* tracker_ = nullptr;
    uint64_t timestamp_ = 0;
    uint32_t tid_ = 0;
    uint32_t cpu_ = 0;

    // These are filled in when the slice is completed.
    uint64_t duration_ = 0;
    StringId thread_name_id_ = 0;
  };

  // A Counter is a trace event that has a value attached to a timestamp.
  // These include CPU frequency ftrace events and systrace trace_marker
  // counter events.
  struct PendingCounter {
   public:
    PendingCounter(SchedTracker*,
                   uint64_t timestamp,
                   double value,
                   StringId name_id,
                   uint32_t ref);
    ~PendingCounter() = default;
    PendingCounter(PendingCounter&& other) noexcept = default;
    PendingCounter& operator=(PendingCounter&& other) = default;
    PendingCounter(PendingCounter& other) = delete;
    PendingCounter& operator=(PendingCounter& other) = delete;

    void Complete(uint64_t end_timestamp, double new_value, RefType ref_type);
    void PushComplete();
    bool IsComplete() const { return duration_ != 0; }

   private:
    // These are filled in when the class is created.
    SchedTracker* tracker_ = nullptr;
    uint64_t timestamp_ = 0;
    double value_ = 0;
    StringId name_id_ = 0;
    uint32_t ref_ = 0;

    // These are filled in when the counter is completed.
    uint64_t duration_ = 0;
    double value_delta_ = 0;
    RefType ref_type_ = RefType::kUTID;
  };

  // Used as the key in |prev_counters_| to find the previous counter with the
  // same ref and name_id.
  struct CounterKey {
    uint64_t ref;      // cpu, utid, ...
    StringId name_id;  // "cpufreq"

    bool operator==(const CounterKey& other) const {
      return (ref == other.ref && name_id == other.name_id);
    }

    struct Hasher {
      size_t operator()(const CounterKey& c) const {
        size_t const h1(std::hash<uint64_t>{}(c.ref));
        size_t const h2(std::hash<size_t>{}(c.name_id));
        return h1 ^ (h2 << 1);
      }
    };
  };

  template <class T>
  void PushCompletedToStorage(std::deque<T>* deque, T* from);

  // Deque of slices which need to completed.
  std::deque<PendingSchedSlice> pending_sched_;

  // Deque of counters which need to completed.
  std::deque<PendingCounter> pending_counters_;

  // Store pending sched slices for each CPU.
  std::array<PendingSchedSlice*, base::kMaxCpus> pending_sched_per_cpu_;

  // Store pending counters for each counter key.
  std::unordered_map<CounterKey, PendingCounter*, CounterKey::Hasher>
      pending_counters_per_key_;

  // Timestamp of the previous event. Used to discard events arriving out
  // of order.
  uint64_t prev_timestamp_ = 0;

  StringId const idle_string_id_;

  TraceProcessorContext* const context_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_SCHED_TRACKER_H_
