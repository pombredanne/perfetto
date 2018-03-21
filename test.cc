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

#include <math.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <stdio.h>

#include "perfetto/base/file_utils.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/string_splitter.h"
#include "src/traced/probes/filesystem/inode_file_data_source.h"
#include "src/traced/probes/filesystem/prefix_finder.h"
#include "src/traced/probes/filesystem/range_tree.h"

// RANGES ARE [x, y), left-inclusive and right-exlusive.
namespace perfetto {

namespace {
constexpr size_t kMaxScans = 40000;
std::string FmtSet(const std::set<std::string>& s);
std::string FmtSet(const std::set<std::string>& s) {
  std::string r;
  for (const auto& x : s) {
    r += " " + x;
  }
  return r;
}

int IOTracingTestMain2(int argc, char** argv);
int IOTracingTestMain2(int argc, char** argv) {
  PrefixFinder pr(kMaxScans);
  RangeTree t;
  {
    std::vector<std::pair<Inode, PrefixFinder::Node*>> inodes;
    ScanFilesDFS("/data", [&pr](BlockDeviceID, Inode i, std::string name,
                                protos::pbzero::InodeFileMap_Entry_Type type) {
      if (type == protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY)
        return;
      pr.AddPath(name);
    });
    ScanFilesDFS(
        "/data", [&pr, &inodes](BlockDeviceID, Inode i, std::string name,
                                protos::pbzero::InodeFileMap_Entry_Type type) {
          if (type == protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY)
            return;
          inodes.emplace_back(i, pr.GetPrefix(name));
        });

    std::sort(inodes.begin(), inodes.end());
    for (const auto& p : inodes)
      t.Insert(p.first, p.second);
  }

  std::string out;
  PERFETTO_CHECK(base::ReadFile(
      std::string("/proc/") + std::to_string(getpid()) + "/smaps_rollup",
      &out));
  std::cout << out << std::endl;

  int wrong = 0;
  int total = 0;
  ScanFilesDFS("/data", [&pr, &t, &wrong, &total](
                            BlockDeviceID, Inode i, std::string name,
                            protos::pbzero::InodeFileMap_Entry_Type type) {
    if (type == protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY)
      return;
    ++total;
    std::set<std::string> found = t.Get(i);
    for (const std::string& s : found) {
      if (name.find(s) == 0)
        return;
    }
    ++wrong;
    std::cout << "Expected: " << name << std::endl;
    std::cout << "Got: " << FmtSet(found) << std::endl;
    std::cout << "Prefix: " << pr.GetPrefix(name)->ToString() << std::endl;
  });
  std::cout << wrong << " / " << total << std::endl;
  return 0;
}
}  // namespace
}  // namespace perfetto

int main(int argc, char** argv) {
  perfetto::IOTracingTestMain2(argc, argv);
}
