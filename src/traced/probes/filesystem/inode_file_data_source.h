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

#ifndef SRC_TRACED_PROBES_FILESYSTEM_INODE_FILE_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_FILESYSTEM_INODE_FILE_DATA_SOURCE_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "perfetto/base/weak_ptr.h"
#include "perfetto/traced/data_source_types.h"
#include "perfetto/tracing/core/basic_types.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "src/traced/probes/filesystem/fs_mount.h"
#include "src/traced/probes/filesystem/lru_inode_cache.h"

#include "perfetto/trace/filesystem/inode_file_map.pbzero.h"

namespace perfetto {

using InodeFileMap = protos::pbzero::InodeFileMap;
class TraceWriter;

void ScanFilesDFS(
    const std::string& root_directory,
    const std::function<void(BlockDeviceID block_device_id,
                             Inode inode_number,
                             const std::string& path,
                             protos::pbzero::InodeFileMap_Entry_Type type)>&);

void CreateDeviceToInodeMap(
    const std::string& root_directory,
    const std::map<BlockDeviceID, std::set<Inode>>& unresolved_inodes,
    LRUInodeCache* cache,
    std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>* block_device_map);

class InodeFileDataSource {
 public:
  InodeFileDataSource(TracingSessionID,
                      std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>*
                          system_partition_files,
                      LRUInodeCache* cache,
                      std::unique_ptr<TraceWriter> writer);

  TracingSessionID session_id() const { return session_id_; }
  base::WeakPtr<InodeFileDataSource> GetWeakPtr() const;

  void OnInodes(const std::vector<std::pair<Inode, BlockDeviceID>>& inodes);

  // If the provided inode number and block device id are found in the
  // block_device_entry map, adds entry to the InodeFileMap proto and returns
  // true.
  bool AddInodeEntryFromMap(
      InodeFileMap* inode_file_map,
      BlockDeviceID block_device_id,
      Inode inode_number,
      const std::map<Inode, InodeMapValue>& block_device_entry);

  // If the provided inode number and block device id are found in the LRU inode
  // cache, adds entry to the InodeFileMap proto and returns true.
  bool AddInodeEntryFromLRU(InodeFileMap* inode_file_map,
                            BlockDeviceID block_device_id,
                            Inode inode_number);

 private:
  const TracingSessionID session_id_;
  std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>*
      system_partition_files_;
  LRUInodeCache* cache_;
  std::multimap<BlockDeviceID, std::string> mount_points_;
  std::unique_ptr<TraceWriter> writer_;
  base::WeakPtrFactory<InodeFileDataSource> weak_factory_;  // Keep last.
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FILESYSTEM_INODE_FILE_DATA_SOURCE_H_
