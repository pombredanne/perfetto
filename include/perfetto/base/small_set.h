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

#ifndef INCLUDE_PERFETTO_BASE_SMALL_SET_H_
#define INCLUDE_PERFETTO_BASE_SMALL_SET_H_

#include <array>

namespace perfetto {

template <typename DataType, size_t Size>
class SmallSet {
 public:
  // Name for consistency with STL.
  using const_iterator = typename std::array<DataType, Size>::const_iterator;
  bool Add(DataType n) {
    if (Contains(n))
      return true;
    if (filled_ < Size) {
      arr_[filled_++] = n;
      return true;
    }
    return false;
  }

  bool Contains(DataType n) const {
    if (!filled_)
      return false;
    for (size_t i = 0; i < filled_; ++i) {
      if (arr_[i] == n)
        return true;
    }
    return false;
  }

  const_iterator begin() const { return arr_.cbegin(); }
  const_iterator end() const { return arr_.cbegin() + filled_; }
  size_t size() const { return filled_; }

 private:
  std::array<DataType, Size> arr_;
  size_t filled_ = 0;
};
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_SMALL_SET_H_
