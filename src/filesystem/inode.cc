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

#include "src/filesystem/inode.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

namespace perfetto {

namespace {
constexpr const char* kMountsPath = "/proc/mounts";
}

std::map<dev_t, std::vector<std::string>> ParseMounts() {
  std::map<dev_t, std::vector<std::string>> device_to_mountpoints;
  std::ifstream f(kMountsPath);
  std::string line;
  std::string mountpoint;
  struct stat buf;
  while (std::getline(f, line)) {
    std::istringstream s(line);
    // Discard first column.
    s >> mountpoint;
    s >> mountpoint;
    if (stat(mountpoint.c_str(), &buf) == -1)
      continue;
    device_to_mountpoints[buf.st_dev].emplace_back(mountpoint);
  }
  return device_to_mountpoints;
}

}  // namespace perfetto
