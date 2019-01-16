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

#include "src/trace_processor/ftrace_utils.h"

#include <math.h>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {

TaskState::TaskState() = default;

TaskState TaskState::From(int16_t raw_state) {
  int16_t masked = raw_state & (kRawMaxTaskState - 1);
  TaskState state;
  state.state_ = std::bitset<kBitsetSize>(static_cast<uint16_t>(masked));
  state.state_[TaskState::kPreemptStateIdx] = raw_state & kRawMaxTaskState;
  state.state_[TaskState::kValidStateIdx] = true;
  return state;
}

char TaskState::AtomToChar(Atom atom) {
  switch (atom) {
    case Atom::kRunnable:
      return 'R';
    case Atom::kInterruptibleSleep:
      return 'S';
    case Atom::kUninterruptibleSleep:
      return 'D';  // D for (D)isk sleep
    case Atom::kStopped:
      return 'T';
    case Atom::kTraced:
      return 't';
    case Atom::kExitDead:
      return 'X';
    case Atom::kZombie:
      return 'Z';
    case Atom::kTaskDead:
      return 'x';
    case Atom::kWakeKill:
      return 'K';
    case Atom::kWaking:
      return 'W';
    case Atom::kParked:
      return 'P';
    case Atom::kNoLoad:
      return 'N';
  }
  PERFETTO_FATAL("For GCC");
}

size_t TaskState::ToString(char* buffer, size_t n) const {
  PERFETTO_CHECK(IsValid() && n > 0);

  // Length available for writing into since we need to account for \0.
  size_t avail_len = n - 1;
  size_t pos = 0;
  size_t count = 0;
  for (size_t i = kInterruptibleSleep; i < kRawMaxTaskState; i <<= 1) {
    size_t idx = static_cast<size_t>(log2(i));
    if (!state_[idx])
      continue;

    bool has_delim = pos > 0;
    bool has_space_for_char = pos < avail_len;
    bool has_space_for_delim = !has_delim || pos + 1 < avail_len;
    if (has_space_for_char && has_space_for_delim) {
      if (has_delim)
        buffer[pos++] = '|';
      buffer[pos++] = AtomToChar(static_cast<Atom>(i));
    }
    count += has_delim ? 2 : 1;
  }

  bool is_runnable = count == 0;
  if (is_runnable && pos < avail_len)
    buffer[pos++] = AtomToChar(Atom::kRunnable);
  count += is_runnable ? 1 : 0;

  if (IsPreempt() && pos < avail_len)
    buffer[pos++] = '+';
  count += IsPreempt() ? 1 : 0;

  buffer[pos] = 0;

  // Note: the return value is the number of bytes we would have written if
  // there was enough space. This is tracked above in count.
  return count;
}

bool TaskState::IsPreempt() const {
  return state_[TaskState::kPreemptStateIdx];
}

bool TaskState::IsValid() const {
  return state_[TaskState::kValidStateIdx];
}

}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto
