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

#include "src/trace_processor/sched_tracker.h"
#include "perfetto/base/utils.h"
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/trace_processor_context.h"

#include <math.h>

namespace perfetto {
namespace trace_processor {

SchedTracker::SchedTracker(TraceProcessorContext* context)
    : idle_string_id_(context->storage->InternString("idle")),
      context_(context) {}

SchedTracker::~SchedTracker() = default;

StringId SchedTracker::GetThreadNameId(uint32_t tid, base::StringView comm) {
  return tid == 0 ? idle_string_id_ : context_->storage->InternString(comm);
}

void SchedTracker::PushSchedSwitch(uint32_t cpu,
                                   uint64_t timestamp,
                                   uint32_t prev_pid,
                                   uint32_t,
                                   base::StringView prev_comm,
                                   uint32_t next_pid) {
  // At this stage all events should be globally timestamp ordered.
  if (timestamp < prev_timestamp_) {
    PERFETTO_ELOG("sched_switch event out of order by %.4f ms, skipping",
                  (prev_timestamp_ - timestamp) / 1e6);
    return;
  }
  prev_timestamp_ = timestamp;
  PERFETTO_DCHECK(cpu < base::kMaxCpus);

  // Get the previous pending slice for this cpu.
  PendingSchedSlice* slice = pending_sched_per_cpu_[cpu];
  if (slice) {
    // If the this events previous pid does not match the previous event's next
    // pid, make a note of this.
    if (prev_pid != slice->tid()) {
      context_->storage->AddMismatchedSchedSwitch();
    }

    slice->Complete(timestamp, prev_comm);
    PushCompletedToStorage(&pending_sched_, slice);
  }

  // Put an element into the deque and add a pointer to this into the cpu map.
  pending_sched_.emplace_back(this, timestamp, next_pid, cpu);
  pending_sched_per_cpu_[cpu] = &pending_sched_.back();
}

void SchedTracker::PushCounter(uint64_t timestamp,
                               double value,
                               StringId name_id,
                               uint64_t ref,
                               RefType ref_type) {
  if (timestamp < prev_timestamp_) {
    PERFETTO_ELOG("counter event out of order by %.4f ms, skipping",
                  (prev_timestamp_ - timestamp) / 1e6);
    return;
  }
  prev_timestamp_ = timestamp;

  // The previous counter with the same ref and name_id.
  const auto& key = CounterKey{ref, name_id};
  PendingCounter* counter = pending_counters_per_key_[key];
  if (counter) {
    counter->Complete(timestamp, value, ref_type);
    PushCompletedToStorage(&pending_counters_, counter);
  }

  // Put an element into the deque and add a pointer to this into the map.
  pending_counters_.emplace_back(this, timestamp, value, ref, name_id);
  pending_counters_per_key_[key] = &pending_counters_.back();
}

template <class T>
void SchedTracker::PushCompletedToStorage(std::deque<T>* deque, T* from) {
  PERFETTO_DCHECK(!deque->empty());

  // If this is not the earliest element, we still need to complete that one
  // so wait for it to finish.
  if (from != &deque->front())
    return;

  // This is the first element so push all the data to storage from this point
  // to the first non-complete item.
  auto it = deque->begin();
  for (; it->IsComplete() && it != deque->end(); it++)
    it->PushComplete();
  deque->erase(deque->cbegin(), it);
}

SchedTracker::PendingSchedSlice::PendingSchedSlice(SchedTracker* tracker,
                                                   uint64_t timestamp,
                                                   uint32_t tid,
                                                   uint32_t cpu)
    : tracker_(tracker), timestamp_(timestamp), tid_(tid), cpu_(cpu) {}

void SchedTracker::PendingSchedSlice::Complete(uint64_t end_timestamp,
                                               base::StringView thread_name) {
  duration_ = end_timestamp - timestamp_;
  thread_name_id_ = tracker_->GetThreadNameId(tid_, thread_name);
}

void SchedTracker::PendingSchedSlice::PushComplete() {
  PERFETTO_DCHECK(IsComplete());

  auto* context = tracker_->context_;
  auto utid =
      context->process_tracker->UpdateThread(timestamp_, tid_, thread_name_id_);
  context->storage->AddSliceToCpu(cpu_, timestamp_, duration_, utid);
}

SchedTracker::PendingCounter::PendingCounter(SchedTracker* tracker,
                                             uint64_t timestamp,
                                             double value,
                                             StringId name_id,
                                             uint32_t ref)
    : tracker_(tracker),
      timestamp_(timestamp),
      value_(value),
      name_id_(name_id),
      ref_(ref) {}

void SchedTracker::PendingCounter::Complete(uint64_t end_timestamp,
                                            double new_value,
                                            RefType ref_type) {
  duration_ = end_timestamp - timestamp_;
  value_delta_ = new_value - value_;
  ref_type_ = ref_type;
}

void SchedTracker::PendingCounter::PushComplete() {
  PERFETTO_DCHECK(IsComplete());

  auto* counters = tracker_->context_->storage->mutable_counters();
  counters->AddCounter(timestamp_, duration_, name_id_, value_, value_delta_,
                       static_cast<int64_t>(ref_), ref_type_);
}

}  // namespace trace_processor
}  // namespace perfetto
