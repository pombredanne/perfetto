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

// RANGES ARE [x, y), left-inclusive and right-exlusive.
namespace perfetto {
namespace {

constexpr size_t kMaxScans = 10000;
constexpr size_t kSetSize = 3;

template <typename T, typename... Args>
void CheckEmplace(T& m, Args&&... args) {
  auto x = m.emplace(args...);
  PERFETTO_DCHECK(x.second);
}

}  // namespace

class SmallSet {
 public:
  using DataType = PrefixFinder::Node*;
  // Name for consistency with STL.
  using const_iterator = std::array<DataType, kSetSize>::const_iterator;
  bool Add(DataType n) {
    if (Contains(n))
      return true;
    if (filled_ < kSetSize) {
      arr_[filled_++] = n;
      return true;
    }
    return false;
  }

  bool Contains(DataType n) const {
    if (!filled_)
      return false;
    for (size_t i = 0; i < filled_; ++i) {
      if (arr_[i] == n)
        return true;
    }
    return false;
  }

  const_iterator begin() const { return arr_.cbegin(); }
  const_iterator end() const { return arr_.cbegin() + filled_; }
  size_t size() const { return filled_; }

 private:
  std::array<DataType, kSetSize> arr_;
  size_t filled_ = 0;
};

class RangeTree {
 public:
  using DataType = PrefixFinder::Node*;

  const std::set<std::string> Get(Inode inode) {
    std::set<std::string> ret;
    auto lower = map_.upper_bound(inode);
    if (lower != map_.begin())
      lower--;
    for (const auto x : lower->second)
      ret.emplace(x->ToString());
    return ret;
  }

  void Insert(Inode inode, DataType interned) {
    auto lower = map_.rbegin();
    if (!map_.empty()) {
      PERFETTO_CHECK(inode > lower->first);
    }

    if (map_.empty() || !lower->second.Add(interned)) {
      SmallSet n;
      n.Add(interned);
      CheckEmplace(map_, inode, std::move(n));
    }
  }

 private:
  std::map<Inode, SmallSet> map_;
};

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
    std::cout << "Got: " << FmtSet(found) << std::endl;
    for (const std::string& s : found) {
      if (name.find(s) == 0)
        return;
    }
    ++wrong;
    std::cout << "Expected: " << name << std::endl;
    std::cout << "Prefix: " << pr.GetPrefix(name)->ToString() << std::endl;
  });
  std::cout << wrong << " / " << total << std::endl;
  return 0;
}

}  // namespace perfetto

int main(int argc, char** argv) {
  perfetto::IOTracingTestMain2(argc, argv);
}
