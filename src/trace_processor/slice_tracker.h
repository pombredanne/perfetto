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

#ifndef SRC_TRACE_PROCESSOR_SLICE_TRACKER_H_
#define SRC_TRACE_PROCESSOR_SLICE_TRACKER_H_

#include <stdint.h>

#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class TraceProcessorContext;

class SliceTracker {
 public:
  explicit SliceTracker(TraceProcessorContext*);
  ~SliceTracker();

  void BeginAndroid(int64_t timestamp,
                    uint32_t ftrace_tid,
                    uint32_t atrace_tgid,
                    StringId cat,
                    StringId name);

  void EndAndroid(int64_t timestamp, uint32_t ftrace_tid, uint32_t atrace_tgid);

  void BeginSyscall(int64_t timestamp, UniqueTid utid, StringId name);
  void EndSyscall(int64_t timestamp, UniqueTid utid, StringId name);

  void Begin(int64_t timestamp, UniqueTid utid, StringId cat, StringId name);

  void Scoped(int64_t timestamp,
              UniqueTid utid,
              StringId cat,
              StringId name,
              int64_t duration);

  void End(int64_t timestamp, UniqueTid utid, StringId cat, StringId name);

  void Flush();

 private:
  using SlicesStack = std::vector<size_t>;

  struct Event {
    enum Kind {
      kStart = 0,
      kEnd,
    };
    int64_t timestamp;
    UniqueTid utid;
    StringId cat;
    StringId name;
    Kind kind;
    bool is_syscall;
  };

  void BeginInternal(int64_t timestamp,
	UniqueTid utid, StringId cat, StringId name, 
	bool is_syscall
	);

  void EndInternal(int64_t timestamp,
           UniqueTid utid,
           StringId cat,
           StringId name,
	   bool is_syscall);

  int64_t GetStackHash(const SlicesStack&);

  SlicesStack* GetStack(UniqueTid utid);

  void SetLastWasSyscall(UniqueTid utid, bool value) {
    if (utid >= last_event_was_syscall_.size())
      last_event_was_syscall_.resize(utid+1);
    last_event_was_syscall_[utid] = value;
  }

  bool GetLastWasSyscall(UniqueTid utid) {
    if (utid >= last_event_was_syscall_.size())
        return false;
    return last_event_was_syscall_[utid];
  }

  void SetIgnoreNext(UniqueTid utid, bool value) {
    if (utid >= ignore_next_.size())
      ignore_next_.resize(utid+1);
    ignore_next_[utid] = value;
  }

  bool GetIgnoreNext(UniqueTid utid) {
    if (utid >= ignore_next_.size())
        return false;
    return ignore_next_[utid];
  }

  std::vector<bool> last_event_was_syscall_;
  std::vector<bool> ignore_next_;
  std::vector<Event> events_;
  std::vector<SlicesStack> utid_to_stack_;
  TraceProcessorContext* const context_;
  std::unordered_map<uint32_t, uint32_t> ftrace_to_atrace_tgid_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_SLICE_TRACKER_H_
