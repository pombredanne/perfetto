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

#include "perfetto/base/file_utils.h"

#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"

namespace perfetto {
namespace base {
namespace {
const size_t kBufSize = 2048;
}

bool ReadFile(const std::string& path, std::string* out) {
  base::ScopedFile fd = base::OpenFile(path.c_str(), O_RDONLY);
  if (!fd) {
    PERFETTO_PLOG("open");
    return false;
  }

  char buf[kBufSize + 1];

  ssize_t bytes_read;
  do {
    bytes_read = PERFETTO_EINTR(read(fd.get(), &buf, kBufSize));
    if (bytes_read > 0) {
      buf[bytes_read] = '\0';
      *out += buf;
    }
  } while (bytes_read > 0);

  if (bytes_read == -1) {
    PERFETTO_PLOG("read");
    return false;
  }
  return true;
}

}  // namespace base
}  // namespace perfetto
