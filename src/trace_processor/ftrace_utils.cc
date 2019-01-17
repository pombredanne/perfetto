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

#include "src/trace_processor/ftrace_utils.h"

#include <algorithm>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {

TaskState::TaskState() = default;

TaskState TaskState::From(uint16_t raw_state) {
  uint16_t masked = raw_state & (kRawMaxTaskState - 1);
  TaskState state;

  // Set the main state bits.
  state.state_ = masked;

  // Set the preemption bit.
  state.state_ |= raw_state & kRawMaxTaskState;

  // Set the valid bit.
  state.state_ |= kValidBitMask;

  return state;
}

TaskState::String TaskState::ToString() const {
  PERFETTO_CHECK(IsValid());

  char buffer[32];
  uint32_t pos = 0;
  for (size_t i = kInterruptibleSleep; i < kRawMaxTaskState; i <<= 1) {
    if (state_ & i)
      buffer[pos++] = AtomToChar(static_cast<Atom>(i));
  }

  if (IsRunnable())
    buffer[pos++] = AtomToChar(Atom::kRunnable);

  if (IsKernelPreempt())
    buffer[pos++] = '+';

  // It is very unlikely that we have used more than the size of the string
  // array. Double check that belief on debug builds.
  PERFETTO_DCHECK(pos < std::tuple_size<TaskState::String>() - 1);

  TaskState::String output{};
  memcpy(output.data(), buffer, std::min(pos, 3u));
  return output;
}

}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto
