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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <string>
#include <tuple>

#include "src/base/test/test_task_runner.h"

namespace perfetto {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::Pointee;

class TestDelegate : public FileScanner::Delegate {
 public:
  TestDelegate(
      std::function<bool(BlockDeviceID,
                         Inode,
                         const std::string&,
                         protos::pbzero::InodeFileMap_Entry_Type)> callback,
      std::function<void()> done_callback)
      : callback_(std::move(callback)),
        done_callback_(std::move(done_callback)) {}
  bool OnInodeFound(BlockDeviceID block_device_id,
                    Inode inode,
                    const std::string& path,
                    protos::pbzero::InodeFileMap_Entry_Type type) override {
    return callback_(block_device_id, inode, path, type);
  }

  void OnInodeScanDone() { return done_callback_(); }

 private:
  std::function<bool(BlockDeviceID,
                     Inode,
                     const std::string&,
                     protos::pbzero::InodeFileMap_Entry_Type)>
      callback_;
  std::function<void()> done_callback_;
};

TEST(TestFileScanner, TestSynchronousStop) {
  uint64_t seen = 0;
  bool done = false;
  TestDelegate delegate(
      [&seen](BlockDeviceID block_device_id, Inode inode,
              const std::string& path,
              protos::pbzero::InodeFileMap_Entry_Type type) {
        ++seen;
        return false;
      },
      [&done] { done = true; });

  FileScanner fs({"src/traced/probes/filesystem/testdata"}, &delegate);
  fs.Scan();

  EXPECT_EQ(seen, 1u);
  EXPECT_TRUE(done);
}

TEST(TestFileScanner, TestAsynchronousStop) {
  uint64_t seen = 0;
  base::TestTaskRunner task_runner;
  TestDelegate delegate(
      [&seen](BlockDeviceID block_device_id, Inode inode,
              const std::string& path,
              protos::pbzero::InodeFileMap_Entry_Type type) {
        ++seen;
        return false;
      },
      task_runner.CreateCheckpoint("done"));

  FileScanner fs({"src/traced/probes/filesystem/testdata"}, &delegate, 1, 1);
  fs.Scan(&task_runner);

  task_runner.RunUntilCheckpoint("done");

  EXPECT_EQ(seen, 1u);
}

}  // namespace
}  // namespace perfetto
