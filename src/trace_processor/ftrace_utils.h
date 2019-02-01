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

#include "perfetto/base/logging.h"
#include "perfetto/base/string_view.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {

// A strongly typed representation of the TaskState enum given in sched_switch
// events.
class TaskState {
 public:
  using TaskStateStr = std::array<char, 4>;

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
    kNewTask = 2048,

    kMaxState = 4096,
    kValid = 0x8000,
  };

  TaskState() = default;
  explicit TaskState(uint16_t raw_state) : state_(raw_state | kValid) {}
  explicit TaskState(const char* state_str);

  // Returns if this TaskState has a valid representation.
  bool is_valid() const { return state_ & kValid; }

  // Returns the string representation of this (valid) TaskState. This array
  // is null terminated.
  // Note: This function CHECKs that |is_valid()| is true.
  TaskStateStr ToString() const;

  // Returns the raw state this class was created from.
  uint16_t raw_state() const {
    PERFETTO_DCHECK(is_valid());
    return state_ & ~kValid;
  }

  // Returns if this TaskState is runnable.
  bool is_runnable() const { return (state_ & (kMaxState - 1)) == 0; }

  // Returns whether kernel preemption caused the exit state.
  bool is_kernel_preempt() const { return state_ & kMaxState; }

 private:
  uint16_t state_ = 0;
};

class StringWriter {
 public:
  StringWriter(char* buffer, size_t n) : buffer_(buffer), n_(n) {}

  void WriteChar(const char in) {
    PERFETTO_DCHECK(pos_ + 1 < n_);
    buffer_[pos_++] = in;
  }

  void WriteString(const char* in, size_t n) {
    PERFETTO_DCHECK(pos_ + n < n_);
    memcpy(&buffer_[pos_], in, n);
    pos_ += n;
  }

  void WriteString(base::StringView data) {
    PERFETTO_DCHECK(pos_ + data.size() < n_);
    memcpy(&buffer_[pos_], data.data(), data.size());
    pos_ += data.size();
  }

  void WriteInt(int64_t value) { WriteZeroPrefixedInt<0>(value); }

  template <size_t PrefixZeros>
  void WriteZeroPrefixedInt(int64_t value) {
    constexpr auto kBufferSize =
        std::numeric_limits<uint64_t>::digits10 + 1 + PrefixZeros;
    PERFETTO_DCHECK(pos_ + kBufferSize + 1 < n_);

    char data[kBufferSize];

    bool negate = value < 0;
    if (negate)
      value = 0 - value;

    uint64_t val = static_cast<uint64_t>(value);
    size_t idx;
    for (idx = kBufferSize - 1; val >= 10;) {
      char digit = val % 10;
      val /= 10;
      data[idx--] = digit + '0';
    }
    data[idx--] = static_cast<char>(val) + '0';

    if (PrefixZeros > 0) {
      size_t num_digits = kBufferSize - 1 - idx;
      for (size_t i = num_digits; i < PrefixZeros; i++) {
        data[idx--] = '0';
      }
    }

    if (negate)
      buffer_[pos_++] = '-';
    for (size_t i = idx + 1; i < kBufferSize; i++)
      buffer_[pos_++] = data[i];
  }

  char* GetCString() {
    PERFETTO_DCHECK(pos_ < n_);
    buffer_[pos_] = '\0';
    return buffer_;
  }

 private:
  char* buffer_ = nullptr;
  size_t n_ = 0;
  size_t pos_ = 0;
};

void FormatSystracePrefix(int64_t timestamp,
                          uint32_t cpu,
                          uint32_t pid,
                          uint32_t tgid,
                          base::StringView name,
                          StringWriter* writer);

}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_FTRACE_UTILS_H_
