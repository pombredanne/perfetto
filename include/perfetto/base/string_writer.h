/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_BASE_STRING_WRITER_H_
#define INCLUDE_PERFETTO_BASE_STRING_WRITER_H_

#include "perfetto/base/logging.h"
#include "perfetto/base/string_view.h"

namespace perfetto {
namespace base {

// A helper class which writes formatted data to a string buffer.
class StringWriter {
 public:
  // Creates a string buffer from a char buffer and length.
  StringWriter(char* buffer, size_t n) : buffer_(buffer), n_(n) {}

  // Writes a char to the buffer.
  void WriteChar(const char in) {
    PERFETTO_DCHECK(pos_ + 1 <= n_);
    buffer_[pos_++] = in;
  }

  // Writes a length delimited string to the buffer.
  void WriteString(const char* in, size_t n) {
    PERFETTO_DCHECK(pos_ + n <= n_);
    memcpy(&buffer_[pos_], in, n);
    pos_ += n;
  }

  // Writes a StringView to the buffer.
  void WriteString(StringView data) {
    PERFETTO_DCHECK(pos_ + data.size() <= n_);
    memcpy(&buffer_[pos_], data.data(), data.size());
    pos_ += data.size();
  }

  // Writes an integer to the buffer.
  void WriteInt(int64_t value) { WritePaddedInt<'0', 0>(value); }

  // Writes an integer to the buffer, padding with |PadChar| if the number of
  // digits of the integer is less than Padding.
  template <char PadChar, size_t Padding>
  void WritePaddedInt(int64_t value) {
    // Need to add 2 to the number of digits to account for minus sign and
    // rounding down of digits10.
    constexpr auto kBufferSize =
        std::numeric_limits<uint64_t>::digits10 + 2 + Padding;
    PERFETTO_DCHECK(pos_ + kBufferSize <= n_);

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

    if (Padding > 0) {
      size_t num_digits = kBufferSize - 1 - idx;
      for (size_t i = num_digits; i < Padding; i++) {
        data[idx--] = PadChar;
      }
    }

    if (negate)
      buffer_[pos_++] = '-';
    for (size_t i = idx + 1; i < kBufferSize; i++)
      buffer_[pos_++] = data[i];
  }

  // Writes a double to the buffer.
  void WriteDouble(double value) {
    // TODO(lalitm): trying to optimize this is premature given we almost never
    // print doubles. Reevaluate this in the future if we do print them more.
    size_t res =
        static_cast<size_t>(snprintf(buffer_ + pos_, n_ - pos_, "%lf", value));
    PERFETTO_DCHECK(pos_ + res < n_);
    pos_ += res;
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

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_STRING_WRITER_H_
