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

class TaskState {
 public:
  enum Atom {
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
    kMax = 12,
  };
  static size_t kMaxStringLength;

  static TaskState From(int16_t raw_state);
  static TaskState Unknown() { return TaskState(); }

  bool ToString(char* buffer, size_t length) const;

 private:
  // Store the information about preempt and valid in the bitset as well to save
  // space.
  static constexpr size_t kPreemptBitsetPos = TaskState::kNoLoad + 1;
  static constexpr size_t kValidBitsetPos = TaskState::kPreemptBitsetPos + 1;
  static constexpr size_t kBitsetSize = TaskState::kValidBitsetPos + 1;

  TaskState();

  static char AtomToChar(Atom);

  std::bitset<TaskState::kBitsetSize> state_;
};

}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_FTRACE_UTILS_H_
