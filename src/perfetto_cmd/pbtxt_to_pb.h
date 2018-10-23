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

#ifndef SRC_PERFETTO_CMD_PBTXT_TO_PB_H_
#define SRC_PERFETTO_CMD_PBTXT_TO_PB_H_

#include <stdint.h>

#include <vector>

#include "google/protobuf/io/zero_copy_stream_impl_lite.h"

namespace perfetto {

class ErrorReporter {
 public:
  ErrorReporter();
  virtual ~ErrorReporter();
  virtual void AddError(size_t line,
                        size_t column_start,
                        size_t column_end,
                        const std::string& message) = 0;
};

std::vector<uint8_t> PbtxtToPb(const std::string& input,
                               ErrorReporter* reporter);

}  // namespace perfetto

#endif  // SRC_PERFETTO_CMD_PBTXT_TO_PB_H_
