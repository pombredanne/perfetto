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

#include "src/traced/probes/filesystem/inode_file_data_source.h"

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <queue>

#include "include/perfetto/ftrace_reader/ftrace_controller.h"
#include "perfetto/base/logging.h"
#include "perfetto/tracing/core/trace_packet.h"

#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

void CreateDeviceToInodeMap(
    const std::string& root_directory,
    std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>* block_device_map,
    const std::map<Inode, BlockDeviceID>& unresolved_inodes,
    std::multimap<BlockDeviceID, std::string> mount_points) {
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
      Inode inode_number = static_cast<Inode>(entry->d_ino);
      struct stat buf;
      if (lstat(filepath.c_str(), &buf) != 0)
        continue;
      BlockDeviceID block_device_id = static_cast<BlockDeviceID>(buf.st_dev);
      // Default
      protos::pbzero::InodeFileMap_Entry_Type type =
          protos::pbzero::InodeFileMap_Entry_Type_UNKNOWN;
      // Readdir and stat not guaranteed to have directory info for all systems
      if (entry->d_type == DT_DIR || S_ISDIR(buf.st_mode)) {
        // Continue iterating through files if current entry is a directory
        queue.push(filepath + filename);
        type = protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY;
      } else if (entry->d_type == DT_REG || S_ISREG(buf.st_mode)) {
        type = protos::pbzero::InodeFileMap_Entry_Type_FILE;
      }

      // If given a non-empty set of inode numbers, only add to the map for the
      // inode numbers provided
      if (!unresolved_inodes.empty()) {
        auto unresolved_inode = unresolved_inodes.find(inode_number);
        if (unresolved_inode == unresolved_inodes.end())
          continue;
        BlockDeviceID provided_block_device_id = unresolved_inode->second;
        PERFETTO_LOG("DATA as inode file path=%s",
                     (filepath + filename).c_str());
        PERFETTO_LOG("GIVEN block device id=%" PRIu64,
                     provided_block_device_id);
        PERFETTO_LOG("CORRECT block device id=%" PRIu64, block_device_id);
        auto range = mount_points.equal_range(block_device_id);
        for (std::multimap<BlockDeviceID, std::string>::iterator it =
                 range.first;
             it != range.second; ++it) {
          PERFETTO_LOG("CORRECT block id mount points=%s", it->second.c_str());
        }
        if (provided_block_device_id != block_device_id)
          continue;
      }

      // Update map
      std::map<Inode, InodeMapValue>& inode_map =
          (*block_device_map)[block_device_id];
      inode_map[inode_number].SetType(type);
      inode_map[inode_number].AddPath(filepath + filename);
    }
    closedir(dir);
  }
}

InodeFileDataSource::InodeFileDataSource(
    SessionID id,
    std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>* file_system_inodes,
    std::unique_ptr<TraceWriter> writer)
    : session_id_(id),
      file_system_inodes_(file_system_inodes),
      writer_(std::move(writer)),
      weak_factory_(this) {}

SessionID InodeFileDataSource::GetSessionID() const {
  return session_id_;
}

base::WeakPtr<InodeFileDataSource> InodeFileDataSource::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

bool InodeFileDataSource::AddInodeFileMapEntry(
    InodeFileMap* inode_file_map,
    BlockDeviceID block_device_id,
    Inode inode_number,
    const std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>&
        block_device_map) {
  auto block_device_entry = block_device_map.find(block_device_id);
  if (block_device_entry != block_device_map.end()) {
    auto inode_map = block_device_entry->second.find(inode_number);
    if (inode_map != block_device_entry->second.end()) {
      auto* entry = inode_file_map->add_entries();
      entry->set_inode_number(inode_number);
      entry->set_type(inode_map->second.type());
      for (const auto& path : inode_map->second.paths())
        entry->add_paths(path.c_str());
      return true;
    }
  }
  return false;
}

void InodeFileDataSource::OnInodes(
    const std::vector<std::pair<Inode, BlockDeviceID>>& inodes) {
  PERFETTO_DLOG("Saw FtraceBundle with %zu inodes.", inodes.size());

  if (mount_points_.empty()) {
    mount_points_ = ParseMounts();
  }
  // Group inodes from FtraceMetadata by block device
  std::map<BlockDeviceID, std::set<Inode>> inode_file_maps;
  for (const auto& inode_and_device_pair : inodes) {
    Inode inode_number = inode_and_device_pair.first;
    BlockDeviceID block_device_id = inode_and_device_pair.second;
    // PERFETTO_LOG("Block device id=%" PRIu64, block_device_id);

    inode_file_maps[block_device_id].emplace(inode_number);
  }
  // Write a TracePacket with an InodeFileMap proto for each block device id
  for (const auto& inode_file_map_data : inode_file_maps) {
    BlockDeviceID block_device_id = inode_file_map_data.first;
    std::set<Inode> inode_numbers = inode_file_map_data.second;

    // New TracePacket for each InodeFileMap
    auto trace_packet = writer_->NewTracePacket();
    auto inode_file_map = trace_packet->set_inode_file_map();

    // Add block device id
    inode_file_map->set_block_device_id(block_device_id);
    PERFETTO_LOG("Block device id=%" PRIu64, block_device_id);

    // Add mount points
    auto range = mount_points_.equal_range(block_device_id);
    for (std::multimap<BlockDeviceID, std::string>::iterator it = range.first;
         it != range.second; ++it) {
      inode_file_map->add_mount_points(it->second.c_str());
      PERFETTO_LOG("Mount points=%s", it->second.c_str());
    }

    // Add entries for each inode number
    std::map<Inode, BlockDeviceID> unresolved_inodes;
    for (const auto& inode_number : inode_numbers) {
      //    PERFETTO_LOG("Inode number=%" PRIu64, inode_number);

      // TODO(azappone): get map entry out here
      bool inSystem = AddInodeFileMapEntry(inode_file_map, block_device_id,
                                           inode_number, *file_system_inodes_);
      // Could not be found in /system partition
      if (!inSystem) {
        PERFETTO_LOG("NOT IN SYSTEM");
        // TODO(azappone): Add LRU and check before adding inode for full scan
        unresolved_inodes.emplace(inode_number, block_device_id);
      }
    }

    // Full scan for any unresolved inodes
    if (!unresolved_inodes.empty()) {
      std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>
          data_partition_files;
      // TODO(azappone): Make root directory a mount point
      std::string root_directory = "/data";
      CreateDeviceToInodeMap(root_directory, &data_partition_files,
                             unresolved_inodes, mount_points_);
      for (const auto& inode_number : unresolved_inodes) {
        bool inData =
            AddInodeFileMapEntry(inode_file_map, block_device_id,
                                 inode_number.first, data_partition_files);
        // Could not be found, just add the inode number
        if (!inData) {
          PERFETTO_LOG("NOT IN DATA EITHER ): ");
          auto* entry = inode_file_map->add_entries();
          entry->set_inode_number(inode_number.first);
        }
      }
    }
    trace_packet->Finalize();
  }
}

}  // namespace perfetto
