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

#ifndef SRC_TRACE_PROCESSOR_FTRACE_UTILS_H_
#define SRC_TRACE_PROCESSOR_FTRACE_UTILS_H_

#include <stddef.h>

#include <bitset>

#include "perfetto/base/optional.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {

// A strongly typed representation of the TaskState enum given in sched_switch
// events.
class TaskState {
 public:
  // Returns a TaskState struct parsed from the raw state integer given.
  static TaskState From(int16_t raw_state);

  // Returns an invalid TaskState struct.
  static TaskState Unknown() { return TaskState(); }

  // Returns if this TaskState has a valid representation.
  bool IsValid() const;

  // Writes the string representation of this (valid) TaskState into |buffer|
  // with |n| the size of the buffer (including space for null terminator). If
  // the buffer does not have enough space to write the full string, the largest
  // possible valid string is written (valid in the sense of e.g. no hanging
  // delimiters). The written string is always null terminated; therefore the
  // max number of characters is n - 1.
  //
  // The return value is the number of bytes written (excluding the null term.)
  // or, if the buffer was too small, the number of bytes that would have been
  // written, had there been enough space in buffer (excluding the null
  // terminator).
  //
  // Note 1: This function CHECKs that |IsValid()| is true.
  // Note 2: The full string was written iff <return value> < n.
  // Note 3: the semantics of this function matches that of snprintf.
  size_t ToString(char* buffer, size_t n) const;

 private:
  enum Atom : uint16_t {
    kRunnable = 0,
    kInterruptibleSleep = 1,
    kUninterruptibleSleep = 2,
    kStopped = 3,
    kTraced = 4,
    kZombie = 5,
    kExitDead = 6,
    kTaskDead = 7,
    kWakeKill = 8,
    kWaking = 9,
    kParked = 10,
    kNoLoad = 11,
    // If you are adding atoms here, make sure to change the constants below.
  };

  // Store the information about preemption and validity of this struct in the
  // bitset.
  // Keep these in sync with Atom above.
  static constexpr size_t kPreemptBitsetPos = 12;
  static constexpr size_t kValidBitsetPos = 13;
  static constexpr size_t kBitsetSize = 14;

  // Keep these in sync with Atom above.
  static constexpr uint16_t kAtomCount = 12;
  static constexpr int16_t kRawMaxTaskState = 1 << (kAtomCount - 1);

  TaskState();

  bool IsPreempt() const;
  static char AtomToChar(Atom);

  std::bitset<TaskState::kBitsetSize> state_;
};

}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_FTRACE_UTILS_H_
