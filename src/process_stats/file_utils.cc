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

#include "src/process_stats/file_utils.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace file_utils {
bool IsNumeric(const char* str) {
  if (!str[0])
    return false;
  for (const char* c = str; *c; c++) {
    if (!isdigit(*c))
      return false;
  }
  return true;
}

void ForEachPidInProcPath(const char* proc_path,
                          const std::function<void(int)>& predicate) {
  DIR* root_dir = opendir(proc_path);
  ScopedDir autoclose(root_dir);
  struct dirent* child_dir;
  while ((child_dir = readdir(root_dir))) {
    if (child_dir->d_type != DT_DIR || !IsNumeric(child_dir->d_name))
      continue;
    predicate(atoi(child_dir->d_name));
  }
}

ssize_t ReadFile(const char* path, char* buf, size_t length) {
  buf[0] = '\0';
  int fd = open(path, O_RDONLY);
  if (fd < 0 && errno == ENOENT)
    return -1;
  ScopedFD autoclose(fd);
  size_t tot_read = 0;
  do {
    ssize_t rsize = read(fd, buf + tot_read, length - tot_read);
    if (rsize == 0)
      break;
    if (rsize == -1 && errno == EINTR)
      continue;
    if (rsize < 0)
      return -1;
    tot_read += static_cast<size_t>(rsize);
  } while (tot_read < length);
  buf[tot_read < length ? tot_read : length - 1] = '\0';
  return static_cast<ssize_t>(tot_read);
}

bool ReadFileTrimmed(const char* path, char* buf, size_t length) {
  ssize_t rsize = ReadFile(path, buf, length);
  if (rsize < 0)
    return false;
  for (ssize_t i = 0; i < rsize; i++) {
    const char c = buf[i];
    if (c == '\0' || c == '\r' || c == '\n') {
      buf[i] = '\0';
      break;
    }
    buf[i] = isprint(c) ? c : '?';
  }
  return true;
}

ssize_t ReadProcFile(int pid, const char* proc_file, char* buf, size_t length) {
  char proc_path[128];
  snprintf(proc_path, sizeof(proc_path), "/proc/%d/%s", pid, proc_file);
  return ReadFile(proc_path, buf, length);
}

// Reads a single-line proc file, stripping out any \0, \r, \n and replacing
// non-printable charcters with '?'.
bool ReadProcFileTrimmed(int pid,
                         const char* proc_file,
                         char* buf,
                         size_t length) {
  char proc_path[128];
  snprintf(proc_path, sizeof(proc_path), "/proc/%d/%s", pid, proc_file);
  return ReadFileTrimmed(proc_path, buf, length);
}

}  // namespace file_utils
