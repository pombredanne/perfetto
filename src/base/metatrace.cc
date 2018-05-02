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

#include "perfetto/base/metatrace.h"

#include <fcntl.h>
#include <stdlib.h>

#include "perfetto/base/time.h"

namespace perfetto {
namespace base {

namespace {
int MaybeOpen(const char* fn) {
  if (fn == nullptr)
    return -1;
  return open(fn, O_WRONLY | O_CREAT | O_TRUNC);
}
}  // namespace

template <>
std::string FormatJSON<std::string>(std::string value) {
  return "\"" + value + "\"";
}

template <>
std::string FormatJSON<const char*>(const char* value) {
  return std::string("\"") + value + "\"";
}

void MetaTrace(std::vector<std::pair<std::string, std::string>> trace) {
  static const char* tracing_path = getenv("PERFETTO_METATRACE_FILE");
  static int fd = MaybeOpen(tracing_path);
  if (fd == -1)
    return;
  std::string data = "{ ";
  for (decltype(trace)::size_type i = 0; i < trace.size(); ++i) {
    const std::pair<std::string, std::string>& p = trace[i];
    data += p.first;
    data += ": ";
    data += p.second;
    data += ", ";
  }
  data += "\"ts\": " + std::to_string(GetWallTimeNs().count() / 1000.) +
          ", \"cat\": \"PERF\"},\n";
  write(fd, data.c_str(), data.size());
}

}  // namespace base
}  // namespace perfetto
