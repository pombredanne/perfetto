/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <array>

#include "perfetto/base/optional.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {

// A strongly typed representation of the TaskState enum given in sched_switch
// events.
class TaskState {
 public:
  using TaskStateStr = std::array<char, 4>;

  // Returns a TaskState struct parsed from the raw state integer given.
  static TaskState From(uint16_t raw_state);

  // Returns an invalid TaskState struct.
  static TaskState Unknown() { return TaskState(); }

  // Returns if this TaskState has a valid representation.
  bool IsValid() const { return state_ & kValidBitMask; }

  // Returns the string representation of this (valid) TaskState. This array
  // is null terminated.
  // Note: This function CHECKs that |IsValid()| is true.
  TaskStateStr ToString() const;

 private:
  // The ordering and values of these fields comes from the kernel in the file
  // https://android.googlesource.com/kernel/msm.git/+/android-msm-wahoo-4.4-pie-qpr1/include/linux/sched.h#212
  enum Atom : uint16_t {
    kRunnable = 0,
    kInterruptibleSleep = 1,
    kUninterruptibleSleep = 2,
    kStopped = 4,
    kTraced = 8,
    kExitDead = 16,
    kExitZombie = 32,
    kTaskDead = 64,
    kWakeKill = 128,
    kWaking = 256,
    kParked = 512,
    kNoLoad = 1024,
    // If you are adding atoms here, make sure to change the constants below.
  };
  static constexpr uint16_t kRawMaxTaskState = 2048;
  static constexpr uint16_t kValidBitMask = 0x8000;

  TaskState();

  bool IsRunnable() const { return (state_ & (kRawMaxTaskState - 1)) == 0; }

  // Returns whether kernel preemption caused the exit state.
  bool IsKernelPreempt() const { return state_ & kRawMaxTaskState; }

  uint16_t state_ = 0;
};

}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_FTRACE_UTILS_H_
