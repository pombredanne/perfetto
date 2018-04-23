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

#ifndef SRC_PROCESS_STATS_PROCFS_UTILS_H_
#define SRC_PROCESS_STATS_PROCFS_UTILS_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace procfs_utils {

struct ThreadInfo {
  int tid = 0;
  char name[16] = {};
};

struct ProcessInfo {
  int pid = 0;
  int ppid = 0;
  bool in_kernel = false;
  bool is_app = false;
  char exe[256] = {};
  std::vector<std::string> cmdline;
  std::map<int, ThreadInfo> threads;
};

bool ReadProcessInfo(int pid, ProcessInfo*, bool stop_recursion = false);

}  // namespace procfs_utils

#endif  // SRC_PROCESS_STATS_PROCFS_UTILS_H_
