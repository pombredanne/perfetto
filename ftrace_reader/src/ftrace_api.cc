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

#include "ftrace_api.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "base/logging.h"
#include "base/utils.h"

namespace perfetto {

FtraceApi::FtraceApi(const std::string& root) : root_(root) {}
FtraceApi::~FtraceApi() = default;

bool FtraceApi::WriteToFile(const std::string& path, const std::string& str) {
  base::ScopedFile fd(open(path.c_str(), O_WRONLY));
  if (!fd)
    return false;
  ssize_t written = PERFETTO_EINTR(write(fd.get(), str.c_str(), str.length()));
  ssize_t length = static_cast<ssize_t>(str.length());
  // This should either fail or write fully.
  PERFETTO_DCHECK(written == length || written == -1);
  return written == length;
}

base::ScopedFile FtraceApi::OpenFile(const std::string& path) {
  return base::ScopedFile(open(path.c_str(), O_RDONLY));
}

size_t FtraceApi::NumberOfCpus() const {
  static size_t num_cpus = sysconf(_SC_NPROCESSORS_CONF);
  return num_cpus;
}

}  // namespace perfetto
