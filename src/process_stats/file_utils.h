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

#ifndef SRC_PROCESS_STATS_FILE_UTILS_H_
#define SRC_PROCESS_STATS_FILE_UTILS_H_

#include <ctype.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <map>
#include <memory>

namespace file_utils {

// TODO(taylori): Migrate to using perfetto:base:ScopedFD.

// RAII classes for auto-releasing fd/dirs.
template <typename RESOURCE_TYPE, int (*CLOSE_FN)(RESOURCE_TYPE)>
struct ScopedResource {
  explicit ScopedResource(RESOURCE_TYPE r) : r_(r) {}
  ~ScopedResource() { CLOSE_FN(r_); }
  RESOURCE_TYPE r_;
};

using ScopedFD = ScopedResource<int, close>;
using ScopedDir = ScopedResource<DIR*, closedir>;

bool IsNumeric(const char* str);

// Invokes predicate(pid) for each folder in |proc_path|/[0-9]+ which has
// a numeric name (typically pids and tids).
void ForEachPidInProcPath(const char* proc_path,
                          const std::function<void(int)>& predicate);

// Reads the contents of |path| fully into |buf| up to |length| chars.
// |buf| is guaranteed to be null terminated.
ssize_t ReadFile(const char* path, char* buf, size_t length);

// Reads a single-line file, stripping out any \0, \r, \n and replacing
// non-printable charcters with '?'. |buf| is guaranteed to be null terminated.
bool ReadFileTrimmed(const char* path, char* buf, size_t length);

// Convenience wrappers for /proc/|pid|/|proc_file| paths.
ssize_t ReadProcFile(int pid, const char* proc_file, char* buf, size_t length);
bool ReadProcFileTrimmed(int pid,
                         const char* proc_file,
                         char* buf,
                         size_t length);

}  // namespace file_utils

#endif  // SRC_PROCESS_STATS_FILE_UTILS_H_
