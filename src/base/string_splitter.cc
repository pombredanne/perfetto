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

#include "perfetto/base/string_splitter.h"

#include <utility>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace base {

StringSplitter::StringSplitter(std::string str, char delimiter)
    : str_(std::move(str)), delimiter_(delimiter) {
  // Accessing str[str.size()] has defined semantic in C++11, hence the +1.
  Initialize(&str_[0], str_.size() + 1);
}

StringSplitter::StringSplitter(char* str, size_t size, char delimiter)
    : delimiter_(delimiter) {
  Initialize(str, size);
}

void StringSplitter::Initialize(char* str, size_t size) {
  PERFETTO_DCHECK(!size || str);
  next_ = str;
  end_ = str + size;
  if (size)
    next_[size - 1] = '\0';
}

const char* StringSplitter::GetNextToken() {
  for (; next_ < end_; next_++) {
    if (*next_ == delimiter_)
      continue;
    char* cur = next_;
    for (;; next_++) {
      if (*next_ == delimiter_) {
        *(next_++) = '\0';
        break;
      }
      if (*next_ == '\0') {
        next_ = end_;
        break;
      }
    }
    return *cur ? cur : nullptr;
  }
  return nullptr;
}

}  // namespace base
}  // namespace perfetto
