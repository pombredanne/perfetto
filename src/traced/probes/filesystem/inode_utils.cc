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

#include "src/traced/probes/filesystem/inode_utils.h"

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <queue>
#include <sstream>

#include "include/perfetto/ftrace_reader/ftrace_controller.h"
#include "perfetto/base/file_utils.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/string_splitter.h"
#include "perfetto/tracing/core/trace_packet.h"

#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

std::multimap<BlockDeviceID, std::string> ParseMounts(const char* path) {
  std::string data;
  if (!base::ReadFile(path, &data)) {
    PERFETTO_ELOG("Failed to read %s", path);
    return {};
  }
  std::multimap<BlockDeviceID, std::string> device_to_mountpoints;

  for (base::StringSplitter lines(std::move(data), '\n'); lines.Next();) {
    base::StringSplitter words(&lines, ' ');
    if (!words.Next() || !words.Next()) {
      PERFETTO_DLOG("Invalid mount point: %s.", lines.cur_token());
      continue;
    }
    const char* mountpoint = words.cur_token();
    struct stat buf {};
    if (stat(mountpoint, &buf) == -1) {
      PERFETTO_PLOG("stat");
      continue;
    }
    device_to_mountpoints.emplace(buf.st_dev, mountpoint);
  }
  tools / fix_include_guard return device_to_mountpoints;
}

void CreateDeviceToInodeMap(const std::string& root_directory,
                            std::map<uint32_t, InodeMap>* block_device_map) {
  // Return immediately if we've already filled in the system map
  if (!block_device_map->empty())
    return;
  std::queue<std::string> queue;
  queue.push(root_directory);
  while (!queue.empty()) {
    struct dirent* entry;
    std::string filepath = queue.front();
    queue.pop();
    DIR* dir = opendir(filepath.c_str());
    filepath += "/";
    if (dir == nullptr)
      continue;
    while ((entry = readdir(dir)) != nullptr) {
      std::string filename = entry->d_name;
      if (filename == "." || filename == "..")
        continue;
      uint64_t inode_number = entry->d_ino;
      struct stat buf;
      if (lstat(filepath.c_str(), &buf) != 0)
        continue;
      uint32_t block_device_id = buf.st_dev;
      InodeMap& inode_map = (*block_device_map)[block_device_id];
      // Default
      Type type = protos::pbzero::InodeFileMap_Entry_Type_UNKNOWN;
      // Readdir and stat not guaranteed to have directory info for all systems
      if (entry->d_type == DT_DIR || S_ISDIR(buf.st_mode)) {
        // Continue iterating through files if current entry is a directory
        queue.push(filepath + filename);
        type = protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY;
      } else if (entry->d_type == DT_REG || S_ISREG(buf.st_mode)) {
        type = protos::pbzero::InodeFileMap_Entry_Type_FILE;
      }
      inode_map[inode_number].first = type;
      inode_map[inode_number].second.emplace(filepath + filename);
    }
    closedir(dir);
  }
}

InodeFileMapDataSource::InodeFileMapDataSource(
    std::map<uint32_t, InodeMap>* file_system_inodes,
    std::unique_ptr<TraceWriter> writer)
    : file_system_inodes_(file_system_inodes), writer_(std::move(writer)) {}

InodeFileMapDataSource::~InodeFileMapDataSource() = default;

void InodeFileMapDataSource::WriteInodes(const FtraceMetadata& metadata) {
  PERFETTO_DLOG("Write Inodes start");

  if (mount_points_.empty()) {
    mount_points_ = ParseMounts();
  }
  // Convert FtraceMetadata into format for InodeFileMap proto
  auto inodes = metadata.inodes;
  std::map<uint32_t, std::set<uint64_t>> inode_file_maps;
  for (const auto& inode : inodes) {
    uint32_t block_device_id = inode.first;
    uint64_t inode_number = inode.second;
    inode_file_maps[block_device_id].emplace(inode_number);
  }
  // Write a TracePacket with an InodeFileMap proto for each block device id
  for (const auto& inode_file_map_data : inode_file_maps) {
    auto trace_packet = writer_->NewTracePacket();
    auto inode_file_map = trace_packet->set_inode_file_map();
    // Add block device id
    uint32_t block_device_id = inode_file_map_data.first;
    inode_file_map->set_block_device_id(block_device_id);
    // Add mount points
    std::pair<Mmap::iterator, Mmap::iterator> range;
    range = mount_points_.equal_range(block_device_id);
    for (Mmap::iterator it = range.first; it != range.second; ++it) {
      inode_file_map->add_mount_points(it->second.c_str());
      PERFETTO_DLOG("Block dev=%" PRIu32 ", Mount point=%s", block_device_id,
                    it->second.c_str());
    }
    // Add entries for each inode number
    std::set<uint64_t> inode_numbers = inode_file_map_data.second;
    for (const auto& inode_number : inode_numbers) {
      PERFETTO_DLOG("Inode number=%" PRIu64, inode_number);
      auto* entry = inode_file_map->add_entries();
      entry->set_inode_number(inode_number);
      auto block_device_map = file_system_inodes_->find(block_device_id);
      if (block_device_map != file_system_inodes_->end()) {
        auto inode_map = block_device_map->second.find(inode_number);
        if (inode_map != block_device_map->second.end()) {
          entry->set_type(inode_map->second.first);
          for (const auto& path : inode_map->second.second)
            entry->add_paths(path.c_str());
        }
      }
    }
    trace_packet->Finalize();
  }
}

}  // namespace perfetto
