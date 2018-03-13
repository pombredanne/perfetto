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

#ifndef SRC_TRACED_PROBES_FILESYSTEM_INODE_UTILS_H_
#define SRC_TRACED_PROBES_FILESYSTEM_INODE_UTILS_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "perfetto/ftrace_reader/ftrace_controller.h"
#include "perfetto/tracing/core/basic_types.h"
#include "perfetto/tracing/core/trace_writer.h"

#include "perfetto/trace/filesystem/inode_file_map.pbzero.h"

namespace perfetto {

// On ARM, st_dev is not dev_t but unsigned long long.
using BlockDeviceID = decltype(stat::st_dev);

constexpr char kMountsPath[] = "/proc/mounts";

std::multimap<BlockDeviceID, std::string> ParseMounts(
    const char* path = kMountsPath);

using InodeMap = std::map<
    uint64_t,
    std::pair<protos::pbzero::InodeFileMap_Entry_Type, std::set<std::string>>>;
using Type = protos::pbzero::InodeFileMap_Entry_Type;
using Mmap = std::multimap<BlockDeviceID, std::string>;

void CreateDeviceToInodeMap(const std::string& root_directory,
                            std::map<uint32_t, InodeMap>* block_device_map);

class InodeFileMapDataSource {
 public:
  explicit InodeFileMapDataSource(
      std::map<uint32_t, InodeMap>* file_system_inodes,
      std::unique_ptr<TraceWriter> writer);
  ~InodeFileMapDataSource();

  void WriteInodes(const FtraceMetadata& metadata);

 private:
  std::map<uint32_t, InodeMap>* file_system_inodes_;
  Mmap mount_points_;
  std::unique_ptr<TraceWriter> writer_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FILESYSTEM_INODE_UTILS_H_
