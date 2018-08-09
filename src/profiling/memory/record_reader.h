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

#ifndef SRC_PROFILING_MEMORY_RECORD_READER_H_
#define SRC_PROFILING_MEMORY_RECORD_READER_H_

#include <functional>
#include <memory>

#include <stdint.h>

#include "perfetto/base/utils.h"

namespace perfetto {

class RecordReader {
 public:
  struct ReceiveBuffer {
    void* data;
    size_t size;
  };

  struct Record {
    std::unique_ptr<uint8_t[]> data;
    size_t size;
  };

  ReceiveBuffer BeginReceive();
  bool EndReceive(size_t recv_size, Record* record) PERFETTO_WARN_UNUSED_RESULT;

 private:
  void Reset();

  size_t read_idx_ = 0;
  uint64_t record_size_ = 0;
  std::unique_ptr<uint8_t[]> buf_;
};

}  // namespace perfetto
#endif  // SRC_PROFILING_MEMORY_RECORD_READER_H_
