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

#include <limits>

#include <stdint.h>

#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/slice_tracker.h"
#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

SliceTracker::SliceTracker(TraceProcessorContext* context)
    : context_(context) {}

SliceTracker::~SliceTracker() = default;

void SliceTracker::BeginAndroid(int64_t timestamp,
                                uint32_t ftrace_tid,
                                uint32_t atrace_tgid,
                                StringId cat,
                                StringId name) {
  UniqueTid utid =
      context_->process_tracker->UpdateThread(ftrace_tid, atrace_tgid);
  ftrace_to_atrace_tgid_[ftrace_tid] = atrace_tgid;
  Begin(timestamp, utid, cat, name);
}

void SliceTracker::EndAndroid(int64_t timestamp,
                              uint32_t ftrace_tid,
                              uint32_t atrace_tgid) {
  auto actual_tgid_it = ftrace_to_atrace_tgid_.find(ftrace_tid);
  if (actual_tgid_it == ftrace_to_atrace_tgid_.end()) {
    // This is possible if we start tracing after a begin slice.
    PERFETTO_DLOG("Unknown tgid for ftrace tid %u", ftrace_tid);
    return;
  }
  uint32_t actual_tgid = actual_tgid_it->second;
  // atrace_tgid can be 0 in older android versions where the end event would
  // not contain the value.
  if (atrace_tgid != 0 && atrace_tgid != actual_tgid) {
    PERFETTO_DLOG("Mismatched atrace pid %u and looked up pid %u", atrace_tgid,
                  actual_tgid);
    context_->storage->IncrementStats(stats::atrace_tgid_mismatch);
  }
  UniqueTid utid =
      context_->process_tracker->UpdateThread(ftrace_tid, actual_tgid);
  End(timestamp, utid);
}

void SliceTracker::Begin(int64_t timestamp,
                         UniqueTid utid,
                         StringId cat,
                         StringId name) {
  events_.push_back(Event{timestamp, utid, cat, name, Event::kStart});
}

void SliceTracker::Scoped(int64_t timestamp,
                          UniqueTid utid,
                          StringId cat,
                          StringId name,
                          int64_t duration) {
  Begin(timestamp, utid, cat, name);
  End(timestamp+duration, utid, cat, name);
}

void SliceTracker::End(int64_t timestamp,
                       UniqueTid utid,
                       StringId cat,
                       StringId name) {
  events_.push_back(Event{timestamp, utid, cat, name, Event::kEnd});
}

void SliceTracker::Flush() {
  std::sort(events_.begin(), events_.end(), [](const Event& a, const Event& b){
      return a.timestamp < b.timestamp;
  });

  auto* slices = context_->storage->mutable_nestable_slices();

  for (const SliceTracker::Event& e : events_) {
    SlicesStack* stack = GetStack(e.utid);
    switch (e.kind) {
      case Event::kStart: {
        const uint8_t depth = static_cast<uint8_t>(stack->size());
        // TODO(hjd): fix
        PERFETTO_CHECK(depth < std::numeric_limits<uint8_t>::max());
        int64_t parent_stack_id = depth == 0 ? 0 : slices->stack_ids()[stack->back()];
        size_t slice_idx = slices->AddSlice(e.timestamp, 0/*duration*/, e.utid, e.cat, e.name, depth, 0, parent_stack_id);
        stack->emplace_back(slice_idx);
        slices->set_stack_id(slice_idx, GetStackHash(*stack));
        break;
      }

      case Event::kEnd: {
        // TODO(hjd): Fix
        //PERFETTO_CHECK(stack->size() > 0);
        if (stack->size() == 0)
          break;
        size_t slice_idx = stack->back();
        int64_t start_ts = slices->start_ns()[slice_idx];
        int64_t duration = e.timestamp - start_ts;
        slices->set_duration(slice_idx, duration);
        stack->pop_back();
        break;
      }
    }
  }
  events_.clear();
}

SliceTracker::SlicesStack* SliceTracker::GetStack(UniqueTid utid) {
  if (utid >= utid_to_stack_.size())
    utid_to_stack_.resize(utid+1);
  return &utid_to_stack_[utid];
}

int64_t SliceTracker::GetStackHash(const SlicesStack& stack) {
  PERFETTO_DCHECK(!stack.empty());

  const auto& slices = context_->storage->nestable_slices();
;
  std::string s;
  s.reserve(stack.size() * sizeof(uint64_t) * 2);
  for (size_t i = 0; i < stack.size(); i++) {
    size_t slice_idx = stack[i];
    s.append(reinterpret_cast<const char*>(&slices.cats()[slice_idx]),
             sizeof(slices.cats()[slice_idx]));
    s.append(reinterpret_cast<const char*>(&slices.names()[slice_idx]),
             sizeof(slices.names()[slice_idx]));
  }
  constexpr uint64_t kMask = uint64_t(-1) >> 1;
  return static_cast<int64_t>((std::hash<std::string>{}(s)) & kMask);
}

}  // namespace trace_processor
}  // namespace perfetto
