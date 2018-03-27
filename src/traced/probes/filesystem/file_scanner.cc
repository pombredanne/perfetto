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

#include "src/traced/probes/filesystem/file_scanner.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "perfetto/base/scoped_file.h"
#include "src/traced/probes/filesystem/inode_file_data_source.h"

namespace perfetto {
namespace {
uint64_t kScanInterval = 10000;  // 30s
uint64_t kScanSteps = 5000;
}  // namespace

FileScanner::FileScanner(
    std::string root_directory,
    std::function<bool(BlockDeviceID block_device_id,
                       Inode inode_number,
                       const std::string& path,
                       protos::pbzero::InodeFileMap_Entry_Type type)> callback,
    std::function<void()> done_callback)
    : callback_(std::move(callback)),
      done_callback_(done_callback),
      queue_({std::move(root_directory)}) {}

void FileScanner::Scan(base::TaskRunner* task_runner) {
  Steps(kScanSteps);
  if (!done()) {
    task_runner->PostDelayedTask([this, task_runner] { Scan(task_runner); },
                                 kScanInterval);
  }
}

void FileScanner::NextDirectory() {
  std::string directory = queue_.back();
  queue_.pop_back();
  current_directory_fd_.reset(opendir(directory.c_str()));
  current_directory_ = std::move(directory);
}

void FileScanner::Step() {
  if (!current_directory_fd_) {
    if (queue_.empty())
      return;
    NextDirectory();
  }

  if (!current_directory_fd_)
    return;

  struct dirent* entry = readdir(current_directory_fd_.get());
  if (entry == nullptr) {
    current_directory_fd_.reset();
    return;
  }

  if (entry->d_type == DT_LNK)
    return;

  std::string filename = entry->d_name;
  if (filename == "." || filename == "..")
    return;
  std::string filepath = current_directory_ + filename;

  struct stat buf;
  if (lstat(filepath.c_str(), &buf) != 0)
    return;

  // This might happen on filesystems that do not return
  // information in entry->d_type.
  if (S_ISLNK(buf.st_mode))
    return;

  Inode inode_number = entry->d_ino;
  BlockDeviceID block_device_id = buf.st_dev;

  protos::pbzero::InodeFileMap_Entry_Type type =
      protos::pbzero::InodeFileMap_Entry_Type_UNKNOWN;
  // Readdir and stat not guaranteed to have directory info for all systems
  if (entry->d_type == DT_DIR || S_ISDIR(buf.st_mode)) {
    // Continue iterating through files if current entry is a directory
    queue_.emplace_back(filepath);
    type = protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY;
  } else if (entry->d_type == DT_REG || S_ISREG(buf.st_mode)) {
    type = protos::pbzero::InodeFileMap_Entry_Type_FILE;
  }

  if (!callback_(block_device_id, inode_number, filepath, type)) {
    queue_.clear();
    current_directory_fd_.reset();
  }
}

void FileScanner::Steps(uint64_t n) {
  for (uint64_t i = 0; i < n && !done(); ++i)
    Step();
}

bool FileScanner::done() {
  return !current_directory_fd_ && queue_.empty();
}
}  // namespace perfetto
