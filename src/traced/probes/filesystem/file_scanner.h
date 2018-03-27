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

#ifndef SRC_TRACED_PROBES_FILESYSTEM_FILE_SCANNER_H_
#define SRC_TRACED_PROBES_FILESYSTEM_FILE_SCANNER_H_

#include "src/traced/probes/filesystem/inode_file_data_source.h"

#include "perfetto/base/scoped_file.h"

#include "perfetto/base/scoped_file.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/traced/data_source_types.h"

#include <string>
#include <vector>

namespace perfetto {

class FileScanner {
  FileScanner(std::string root_directory,
              std::function<bool(BlockDeviceID block_device_id,
                                 Inode inode_number,
                                 const std::string& path,
                                 protos::pbzero::InodeFileMap_Entry_Type type)>
                  callback,
              std::function<void()> done_callback);
  void Scan(base::TaskRunner* task_runner);

 private:
  void NextDirectory();
  void Step();
  void Steps(uint64_t n);
  bool done();

  std::function<bool(BlockDeviceID block_device_id,
                     Inode inode_number,
                     const std::string& path,
                     protos::pbzero::InodeFileMap_Entry_Type type)>
      callback_;
  std::function<void()> done_callback_;
  std::vector<std::string> queue_;
  base::ScopedDir current_directory_fd_;
  std::string current_directory_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FILESYSTEM_FILE_SCANNER_H_
