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

// RANGES ARE [x, y), left-inclusive and right-exlusive.
namespace perfetto {
namespace {

constexpr size_t kMaxScans = 20000;
constexpr size_t kSetSize = 3;

template <typename T, typename... Args>
void CheckEmplace(T& m, Args&&... args) {
  auto x = m.emplace(args...);
  PERFETTO_DCHECK(x.second);
}

}  // namespace

// TODO(fmayer): Find a name for this.
// This needs stable iteration order!
class Prefixes {
 public:
  // Opaque placeholder for a prefix that can be turned into a string
  // using .ToString.
  // Can not be constructed outside of Prefixes.
  class Node {
   public:
    friend class Prefixes;

    Node(const Node& that) = delete;
    Node& operator=(const Node&) = delete;

    std::string ToString() {
      if (parent_ != nullptr)
        return parent_->ToString() + "/" + name_;
      return name_;
    }

   private:
    Node(std::string name, Node* parent) : name_(name), parent_(parent) {}

    std::string name_;
    std::map<std::string, std::unique_ptr<Node>> children_;
    Node* parent_;
  };

  Prefixes(size_t limit) : limit_(limit) {}

  void AddPath(std::string path) {
    perfetto::base::StringSplitter s(std::move(path), '/');
    for (size_t i = 0; s.Next(); ++i) {
      char* token = s.cur_token();
      if (i < state_.size()) {
        std::pair<std::string, size_t>& elem = state_[i];
        if (elem.first == token) {
          elem.second++;
        } else {
          if (i == 0 || state_[i - 1].second > limit_) {
            Node* cur = &root_;
            for (auto it = state_.cbegin();
                 it != state_.cbegin() + static_cast<ssize_t>(i + 1); it++) {
              std::unique_ptr<Node>& next = cur->children_[it->first];
              if (!next)
                next.reset(new Node(it->first, cur));
              cur = next.get();
            }
          }
          elem.first = token;
          elem.second = 1;
          state_.resize(i + 1);
        }
      } else {
        state_.emplace_back(token, 1);
      }
    }
  }

  Node* GetPrefix(std::string path) {
    perfetto::base::StringSplitter s(std::move(path), '/');
    Node* cur = &root_;
    for (size_t i = 0; s.Next(); ++i) {
      char* token = s.cur_token();
      auto it = cur->children_.find(token);
      if (it == cur->children_.end())
        break;
      cur = it->second.get();
      PERFETTO_DCHECK(cur->name_ == token);
    }
    return cur;
  }

 private:
  const size_t limit_;
  // (path element, count) tuples for last path seen.
  std::vector<std::pair<std::string, size_t>> state_;
  Node root_{"", nullptr};
};

class SmallSet {
 public:
  using DataType = Prefixes::Node*;
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
  using DataType = Prefixes::Node*;

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
  Prefixes pr(kMaxScans);
  RangeTree t;
  {
    std::map<Inode, Prefixes::Node*> mp;
    ScanFilesDFS(
        "/data", [&pr, &mp](BlockDeviceID, Inode i, std::string name,
                            protos::pbzero::InodeFileMap_Entry_Type type) {
          if (type == protos::pbzero::InodeFileMap_Entry_Type_DIRECTORY)
            return;
          pr.AddPath(name);
          mp.emplace(i, pr.GetPrefix(name));
        });
    for (const auto& p : mp)
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

}  // namespace perfetto

int main(int argc, char** argv) {
  perfetto::IOTracingTestMain2(argc, argv);
}
