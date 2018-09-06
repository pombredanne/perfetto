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

#include "src/profiling/memory/client.h"

#include <inttypes.h>
#include <sys/socket.h>
#include <atomic>
#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"
#include "src/profiling/memory/transport_data.h"

namespace perfetto {
namespace {

std::atomic<uint64_t> global_sequence_number(0);
constexpr size_t kFreePageBytes = 4096;
constexpr size_t kFreePageSize = kFreePageBytes / sizeof(uint64_t);

}  // namespace

FreePage::FreePage() : free_page_(kFreePageSize) {
  free_page_[0] = static_cast<uint64_t>(kFreePageBytes);
  free_page_[1] = static_cast<uint64_t>(RecordType::Free);
  offset_ = 2;
  // Code in Add assumes that offset is aligned to 2.
  PERFETTO_DCHECK(offset_ % 2 == 0);
}

void FreePage::Add(const void* addr, int fd) {
  std::lock_guard<std::mutex> l(mtx_);
  if (offset_ == kFreePageSize)
    Flush(fd);
  static_assert(kFreePageSize % 2 == 0,
                "free page size needs to be divisible by two");
  free_page_[offset_++] = reinterpret_cast<uint64_t>(++global_sequence_number);
  free_page_[offset_++] = reinterpret_cast<uint64_t>(addr);
}

bool FreePage::Flush(int fd) {
  size_t written = 0;
  do {
    ssize_t wr = PERFETTO_EINTR(send(fd, &free_page_[0] + written,
                                     kFreePageBytes - written, MSG_NOSIGNAL));
    if (wr == -1 && errno != EINTR) {
      return false;
    }
    written += static_cast<size_t>(wr);
  } while (written < kFreePageBytes);
  offset_ = 3;
  return true;
}

}  // namespace perfetto
