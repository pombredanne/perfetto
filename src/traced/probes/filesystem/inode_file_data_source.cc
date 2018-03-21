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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <queue>

#include "perfetto/base/logging.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/trace_writer.h"

#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

void ScanFilesDFS(
    const std::string& root_directory,
    const std::function<void(BlockDeviceID block_device_id,
                             Inode inode_number,
                             const std::string& path,
                             protos::pbzero::InodeFileMap_Entry_Type type)>&
        fn) {
  std::vector<std::string> queue{root_directory};
  while (!queue.empty()) {
    struct dirent* entry;
    std::string filepath = queue.back();
    queue.pop_back();
    DIR* dir = opendir(filepath.c_str());
    filepath += "/";
    if (dir == nullptr)
      continue;
    while ((entry = readdir(dir)) != nullptr) {
      std::string filename = entry->d_name;
      if (filename == "." || filename == "..")
        continue;

      struct stat buf;
      if (lstat(filepath.c_str(), &buf) != 0)
        continue;

      Inode inode_number = entry->d_ino;
      BlockDeviceID block_device_id = buf.st_dev;

      protos::pbzero::InodeFileMap_Entry_Type type =
          protos::pbzero::InodeFileMap_Entry_Type_UNKNOWN;
      // Readdir and stat not guaranteed to have directory info for all systems
      if (entry->d_type == DT_DIR || S_ISDIR(buf.st_mode)) {
        // Continue iterating through files if current entry is a directory
        queue.push_back(filepath + filename);
        type = protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY;
      } else if (entry->d_type == DT_REG || S_ISREG(buf.st_mode)) {
        type = protos::pbzero::InodeFileMap_Entry_Type_FILE;
      }

      fn(block_device_id, inode_number, filepath + filename, type);
    }
    closedir(dir);
  }
}

void CreateDeviceToInodeMap(
    const std::string& root_directory,
    const std::map<BlockDeviceID, std::set<Inode>>& unresolved_inodes,
    LRUInodeCache* cache,
    std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>* block_device_map) {
  ScanFilesDFS(
      root_directory, [&block_device_map, &unresolved_inodes, cache](
                          BlockDeviceID block_device_id, Inode inode_number,
                          const std::string& path,
                          protos::pbzero::InodeFileMap_Entry_Type type) {

        // If given a non-empty set of inode numbers, only add to the map for
        // the inode numbers provided
        if (!unresolved_inodes.empty()) {
          auto block_device_entry = unresolved_inodes.find(block_device_id);
          if (block_device_entry == unresolved_inodes.end())
            return;
          auto unresolved_inode = block_device_entry->second.find(inode_number);
          if (unresolved_inode == block_device_entry->second.end())
            return;
          std::pair<BlockDeviceID, Inode> key{block_device_id, inode_number};
          auto currVal = cache->Get(key);
          std::set<std::string> paths;
          if (currVal != nullptr)
            paths = currVal->paths();
          paths.emplace(path);
          InodeMapValue value(type, paths);
          cache->Insert(key, value);
        }

        std::map<Inode, InodeMapValue>& inode_map =
            (*block_device_map)[block_device_id];
        inode_map[inode_number].SetType(type);
        inode_map[inode_number].AddPath(path);
      });
}

InodeFileDataSource::InodeFileDataSource(
    TracingSessionID id,
    std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>*
        system_partition_files,
    LRUInodeCache* cache,
    std::unique_ptr<TraceWriter> writer)
    : session_id_(id),
      system_partition_files_(system_partition_files),
      cache_(cache),
      writer_(std::move(writer)),
      weak_factory_(this) {}

bool InodeFileDataSource::AddInodeEntryFromMap(
    InodeFileMap* inode_file_map,
    BlockDeviceID block_device_id,
    Inode inode_number,
    const std::map<Inode, InodeMapValue>& block_device_entry) {
  auto inode_map = block_device_entry.find(inode_number);
  if (inode_map == block_device_entry.end())
    return false;
  auto* entry = inode_file_map->add_entries();
  entry->set_inode_number(inode_number);
  entry->set_type(inode_map->second.type());
  for (const auto& path : inode_map->second.paths())
    entry->add_paths(path.c_str());
  return true;
}

bool InodeFileDataSource::AddInodeEntryFromLRU(InodeFileMap* inode_file_map,
                                               BlockDeviceID block_device_id,
                                               Inode inode_number) {
  auto value = cache_->Get(std::make_pair(block_device_id, inode_number));
  if (value == nullptr)
    return false;
  auto* entry = inode_file_map->add_entries();
  entry->set_inode_number(inode_number);
  entry->set_type(value->type());
  for (const auto& path : value->paths())
    entry->add_paths(path.c_str());
  return true;
}

void InodeFileDataSource::OnInodes(
    const std::vector<std::pair<Inode, BlockDeviceID>>& inodes) {
  PERFETTO_DLOG("Saw FtraceBundle with %zu inodes.", inodes.size());
  if (mount_points_.empty()) {
    mount_points_ = ParseMounts();
  }
  // Group inodes from FtraceMetadata by block device
  std::map<BlockDeviceID, std::set<Inode>> inode_file_maps;
  for (const auto& inodes_pair : inodes) {
    Inode inode_number = inodes_pair.first;
    BlockDeviceID block_device_id = inodes_pair.second;
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

    // Add mount points
    auto range = mount_points_.equal_range(block_device_id);
    for (std::multimap<BlockDeviceID, std::string>::iterator it = range.first;
         it != range.second; ++it)
      inode_file_map->add_mount_points(it->second.c_str());

    // Add entries for inodes in system
    std::map<BlockDeviceID, std::set<Inode>> data_partition_inodes;
    bool block_device_in_system = false;
    auto system_entry = system_partition_files_->find(block_device_id);
    if (system_entry != system_partition_files_->end())
      block_device_in_system = true;

    uint64_t unresolved_count = 0;
    uint64_t cache_found_count = 0;
    PERFETTO_DLOG("Found %lu total inodes",
                  static_cast<uint64_t>(inode_numbers.size()));
    for (const auto& inode_number : inode_numbers) {
      bool inode_in_system = false;
      if (block_device_in_system) {
        // Search in /system partition and add to InodeFileMap
        inode_in_system =
            AddInodeEntryFromMap(inode_file_map, block_device_id, inode_number,
                                 system_entry->second);
      }
      if (!block_device_in_system || !inode_in_system) {
        bool inode_in_LRU =
            AddInodeEntryFromLRU(inode_file_map, block_device_id, inode_number);
        if (!inode_in_LRU) {
          unresolved_count++;
          data_partition_inodes[block_device_id].emplace(inode_number);
        } else {
          cache_found_count++;
        }
      }
    }
    PERFETTO_DLOG("%lu inodes found in cache", cache_found_count);
    PERFETTO_DLOG("%lu inodes for full file scan", unresolved_count);
    // Full scan for unresolved inodes in the /data partition
    // Currently only enabled if we've seen over 10 unresolved inodes since we
    // are not filtering our own scanning
    if (!data_partition_inodes.empty() && data_partition_inodes.size() > 10) {
      std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>
          data_partition_files;
      // TODO(azappone): Make root directory a mount point
      std::string root_directory = "/data";
      CreateDeviceToInodeMap(root_directory, data_partition_inodes, cache_,
                             &data_partition_files);

      bool block_device_in_data = false;
      auto data_entry = data_partition_files.find(block_device_id);
      if (data_entry != data_partition_files.end())
        block_device_in_data = true;
      for (const auto& data_partition_inode :
           data_partition_inodes[block_device_id]) {
        bool inode_in_data = false;
        if (block_device_in_data) {
          // Search in /data partition and add to InodeFileMap if found
          inode_in_data =
              AddInodeEntryFromMap(inode_file_map, block_device_id,
                                   data_partition_inode, data_entry->second);
        }
        // Could not be found, just add the inode number
        if (!block_device_in_data || !inode_in_data) {
          auto* entry = inode_file_map->add_entries();
          entry->set_inode_number(data_partition_inode);
        }
      }
    }
    trace_packet->Finalize();
  }
}

base::WeakPtr<InodeFileDataSource> InodeFileDataSource::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

}  // namespace perfetto
