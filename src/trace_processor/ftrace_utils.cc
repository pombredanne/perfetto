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
#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {

TaskState::TaskState() = default;

TaskState TaskState::From(int16_t raw_state) {
  int16_t masked = raw_state & (Atom::kMax - 1);
  TaskState state;
  state.state_ = std::bitset<kBitsetSize>(static_cast<uint16_t>(masked));
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
    case Atom::kZombie:
      return 'Z';
    case Atom::kExitDead:
      return 'X';
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
    case Atom::kMax:
      PERFETTO_FATAL("kMax is not a valid atom to convert");
  }
  PERFETTO_FATAL("For GCC");
}

bool TaskState::ToString(char* buffer, size_t length) const {
  PERFETTO_CHECK(state_[TaskState::kValidBitsetPos] && length > 0);

  bool written_all = true;
  size_t pos = 0;
  for (size_t i = 0; i < Atom::kMax; i++) {
    Atom atom = static_cast<Atom>(i);
    if (!state_[atom])
      continue;

    bool write_delim = pos > 0;
    if (pos >= length || (write_delim && pos + 1 >= length)) {
      written_all = false;
      break;
    }

    if (write_delim)
      buffer[pos++] = '-';
    buffer[pos++] = AtomToChar(static_cast<Atom>(i));
  }

  size_t null_pos = pos < length ? pos : length - 1;
  buffer[null_pos] = '\0';
  return written_all;
}

}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto
