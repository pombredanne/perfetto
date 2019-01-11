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

#include <stddef.h>
#include <stdint.h>

#include "perfetto/base/file_utils.h"
#include "perfetto/base/temp_file.h"
#include "src/profiling/memory/shared_ring_buffer.h"

namespace perfetto {
namespace profiling {
namespace {

int FuzzRingBuffer(const uint8_t* data, size_t size) {
  auto fd = base::TempFile::CreateUnlinked().ReleaseFD();
  PERFETTO_CHECK(fd);
  PERFETTO_CHECK(base::WriteAll(*fd, data, size) == static_cast<ssize_t>(size));
  PERFETTO_CHECK(lseek(*fd, 0, SEEK_SET) != -1);
  auto buf = SharedRingBuffer::Attach(std::move(fd));
  if (!buf)
    return 0;
  auto read_buf = buf->Read();
  return 0;
}

}  // namespace
}  // namespace profiling
}  // namespace perfetto

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  return perfetto::profiling::FuzzRingBuffer(data, size);
}
