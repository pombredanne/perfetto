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

#include "perfetto/base/logging.h"
#include "perfetto/base/string_splitter.h"
#include "src/traced/probes/filesystem/inode_file_data_source.h"

// RANGES ARE [x, y), left-inclusive and right-exlusive.
namespace perfetto {
namespace {

constexpr size_t kMaxScans = 50000;
constexpr int kSortedBatchSize = 1;
constexpr uint64_t kMergeDistance = 0;
constexpr size_t kSetSize = 3;

// Random util.
template <typename T, typename... Args>
void CheckEmplace(T& m, Args&&... args) {
  auto x = m.emplace(args...);
  PERFETTO_DCHECK(x.second);
}

template <typename T>
T CheckedNonNegativeSubtraction(T a, T b) {
  PERFETTO_DCHECK(b <= a);
  return a - b;
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

  // private:
  const size_t limit_;
  // (path element, count) tuples for last path seen.
  std::vector<std::pair<std::string, size_t>> state_;
  Node root_{"", nullptr};
};

class SmallSet {
 public:
  using DataType = Prefixes::Node*;

  bool Add(DataType n) {
    if (Contains(n))
      return true;
    if (filled_ < kSetSize) {
      arr_[filled_++] = n;
      return true;
    }
    return false;
  }

  bool Contains(DataType n) {
    if (!filled_)
      return false;
    for (size_t i = 0; i < filled_; ++i) {
      if (arr_[i] == n)
        return true;
    }
    return false;
  }

 private:
  std::array<DataType, kSetSize> arr_;
  size_t filled_;
};

class RangeTree {
 public:
  using DataType = Prefixes::Node*;

  const std::set<std::string> Get(uint64_t inode) {
    std::set<std::string> ret;
    auto surr = GetSurrounding(inode);
    auto lower = surr.first;
    auto upper = surr.second;
    auto overlap = GetOverlap(lower, upper, inode);

    switch (overlap) {
      case SurroundingRanges::kUpper:
        for (const auto x : upper->second.second)
          ret.emplace(x->ToString());
        break;
      case SurroundingRanges::kLower:
        for (const auto x : lower->second.second)
          ret.emplace(x->ToString());
        break;
      case SurroundingRanges::kNone:
        break;
    }
    return ret;
  }

  bool CanMergeSets(const std::set<DataType>& a,
                    const std::set<DataType>& b,
                    DataType y) {
    std::set<DataType> c;
    for (const auto x : a)
      c.emplace(x);
    for (const auto x : b)
      c.emplace(x);
    c.emplace(y);
    return c.size() <= kSetSize;
  }

  void Insert(uint64_t inode, DataType interned) {
    const auto surr = GetSurrounding(inode);
    const auto& lower = surr.first;
    const auto& upper = surr.second;

    // Inserting in-between two ranges that can be merged.
    if (ConsiderLower(lower, upper) && ConsiderUpper(lower, upper) &&
        CanMergeSets(lower->second.second, upper->second.second, interned)) {
      for (const DataType x : upper->second.second) {
        AddToSet(lower->second.second, x);
      }
      AddToSet(lower->second.second, interned);
      lower->second.first = upper->second.first;
      map_.erase(upper);
      return;
    }

    // Simplify code below by ensuring that we never match the borders
    // of a range by moving them away and then retrying. This is slightly
    // more inefficient than incorporating this into the logic below.
    if (ConsiderUpper(lower, upper) && inode == upper->first) {
      if (upper->second.first > inode + 1)
        CheckEmplace(map_, inode + 1, upper->second);
      map_.erase(upper);
      return Insert(inode, interned);
    }
    if (ConsiderLower(lower, upper) && lower->second.first == inode + 1) {
      lower->second.first = inode;
      return Insert(inode, interned);
    }

    const std::pair<uint64_t, DataType> data{inode, interned};
    auto merge_strategy = GetMergeStrategy(lower, upper, data);
    auto overlap = GetOverlap(lower, upper, inode);

    // We can only merge with the following range if we don't overlap
    // with the previous one.
    PERFETTO_DCHECK(overlap != SurroundingRanges::kUpper);
    PERFETTO_DCHECK(merge_strategy != SurroundingRanges::kUpper ||
                    overlap == SurroundingRanges::kNone);

    if (merge_strategy == SurroundingRanges::kNone &&
        overlap == SurroundingRanges::kLower) {
      auto n = lower->second.first;
      lower->second.first = inode;
      if (n > inode + 1)
        CheckEmplace(map_, inode + 1, std::make_pair(n, lower->second.second));
      if (lower->first == inode)
        map_.erase(inode);
    }
#if PERFETTO_DCHECK_IS_ON()
    auto nsurr = GetSurrounding(inode);
#endif
    switch (merge_strategy) {
      case SurroundingRanges::kNone:
        CheckEmplace(map_, inode,
                     std::make_pair(inode + 1, std::set<DataType>{interned}));
        break;
      case SurroundingRanges::kLower:
#if PERFETTO_DCHECK_IS_ON()
        PERFETTO_DCHECK(lower == nsurr.first);
#endif
        lower->second.first = std::max(lower->second.first, inode + 1);
        AddToSet(lower->second.second, interned);
        break;
      case SurroundingRanges::kUpper:
#if PERFETTO_DCHECK_IS_ON()
        PERFETTO_DCHECK(upper == nsurr.second);
#endif
        AddToSet(upper->second.second, interned);
        if (inode != upper->first) {
          CheckEmplace(map_, inode, upper->second);
          map_.erase(upper);
        }
        break;
    }
  }

  // private:
  using MapType = std::map<uint64_t, std::pair<uint64_t, std::set<DataType>>>;
  MapType map_;

  enum class SurroundingRanges {
    kNone = 0,
    kLower = 1,
    kUpper = 2,
  };

  bool ConsiderLower(const MapType::iterator& lower,
                     const MapType::iterator& upper) {
    return upper != lower;
  }

  bool ConsiderUpper(const MapType::iterator& lower,
                     const MapType::iterator& upper) {
    return upper != map_.cend();
  }

  bool IsInSet(const std::set<DataType>& s, DataType e) {
    return s.count(e) == 1;
  }

  void AddToSet(std::set<DataType>& s, DataType e) {
    if (IsInSet(s, e)) {
      return;
    }
    PERFETTO_DCHECK(s.size() < kSetSize);
    s.insert(e);
    return;
  }

  SurroundingRanges GetOverlap(const MapType::iterator& lower,
                               const MapType::iterator& upper,
                               uint64_t inode) {
    if (upper != map_.end() && inode == upper->first)
      return SurroundingRanges::kUpper;
    if (ConsiderLower(lower, upper) && inode < lower->second.first)
      return SurroundingRanges::kLower;
    return SurroundingRanges::kNone;
  }

  SurroundingRanges GetMergeStrategy(
      const MapType::iterator& lower,
      const MapType::iterator& upper,
      const std::pair<uint64_t, DataType>& data) {
    PERFETTO_DCHECK(
        (!ConsiderLower(lower, upper) || data.first >= lower->first) &&
        (!ConsiderUpper(lower, upper) || data.first < upper->second.first));
    // Always prefer merging with lower as that is cheaper to do.

    // We cannot merge with the upper range if we overlap with the lower.
    auto overlap = GetOverlap(lower, upper, data.first);
    bool consider_upper = ConsiderUpper(lower, upper);
    if (overlap == SurroundingRanges::kLower)
      consider_upper = false;

    // Only merge up to kMergeDistance in order to prevent huge ranges.
    // In theory, if the first and the last element come after each other,
    // they will be merged into a range spanning the entire space, which will
    // have to be split for every insert. Needless to say, this would be bad.
    if (consider_upper && kMergeDistance != 0 &&
        CheckedNonNegativeSubtraction(upper->first, data.first) >
            kMergeDistance)
      consider_upper = false;

    bool consider_lower = ConsiderLower(lower, upper);
    if (consider_lower && kMergeDistance != 0 &&
        data.first > lower->second.first &&
        CheckedNonNegativeSubtraction(data.first, lower->second.first) >
            kMergeDistance)
      consider_lower = false;

    // If the folder is already contained in either of them, merge into that.
    // Prefer lower.
    if (consider_lower && IsInSet(lower->second.second, data.second))
      return SurroundingRanges::kLower;
    if (consider_upper && IsInSet(upper->second.second, data.second))
      return SurroundingRanges::kUpper;

    size_t lower_occupation = kSetSize;
    if (consider_lower)
      lower_occupation = lower->second.second.size();
    size_t upper_occupation = kSetSize;
    if (consider_upper)
      upper_occupation = upper->second.second.size();

    // Otherwise, merge into the less occupied. Prefer lower.
    if (upper_occupation < lower_occupation)
      return SurroundingRanges::kUpper;
    if (lower_occupation != kSetSize)
      return SurroundingRanges::kLower;

    // Otherwise, do not merge.
    return SurroundingRanges::kNone;
  }

  std::pair<MapType::iterator, MapType::iterator> GetSurrounding(
      uint64_t inode) {
    auto upper = map_.lower_bound(inode);
    auto lower = upper;
    if (upper != map_.begin()) {
      lower--;
    }
    PERFETTO_DCHECK(!ConsiderLower(lower, upper) || lower != upper);
    return {lower, upper};
  }
};

size_t GetPeakRSS();
size_t GetPeakRSS() {
  struct rusage r;
  getrusage(RUSAGE_SELF, &r);
  return static_cast<size_t>(r.ru_maxrss * 1024L);
}

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
  ScanFilesDFS("/data", [&pr](BlockDeviceID, Inode i, std::string name,
                              protos::pbzero::InodeFileMap_Entry_Type) {
    pr.AddPath(name);
  });

  std::cout << "PID: " << getpid() << std::endl;
  sleep(3600);

  RangeTree t;
  ScanFilesDFS("/data", [&t, &pr](BlockDeviceID, Inode i, std::string name,
                                  protos::pbzero::InodeFileMap_Entry_Type) {
    t.Insert(i, pr.GetPrefix(name));
  });

  std::cout << "RSS " << GetPeakRSS() << std::endl;

  int wrong = 0;
  int total = 0;
  ScanFilesDFS("/data",
               [&t, &wrong, &total](BlockDeviceID, Inode i, std::string name,
                                    protos::pbzero::InodeFileMap_Entry_Type) {
                 ++total;
                 std::set<std::string> found = t.Get(i);
                 for (const std::string& s : found) {
                   if (name.find(s) == 0)
                     return;
                 }
                 ++wrong;
                 /*
                 std::cout << "Expected: " << name << std::endl;
                 std::cout << "Got: " << FmtSet(found) << std::endl;
                 std::cout << "Prefix: " << pr.GetPrefix(name)->ToString() <<
                 std::endl;
                 */
               });
  std::cout << wrong << " / " << total << std::endl;
  return 0;
}

int IOTracingTestMain(int argc, char** argv);
int IOTracingTestMain(int argc, char** argv) {
  std::string input_file = "finodes";
  if (argc > 1)
    input_file = argv[1];

  auto orss = GetPeakRSS();

  Prefixes pr(kMaxScans);
  std::ifstream f0(input_file);
  {
    std::string line;
    while (std::getline(f0, line)) {
      std::istringstream ss(line);
      uint64_t inode;
      std::string name;
      ss >> inode;
      ss >> name;
      if (!name.empty())
        pr.AddPath(name);
    }
  }

  std::ifstream f(input_file);
  RangeTree t;
  std::priority_queue<std::pair<uint64_t, std::string>,
                      std::vector<std::pair<uint64_t, std::string>>,
                      std::greater<std::pair<uint64_t, std::string>>>
      pq;
  for (int i = 0; i < kSortedBatchSize; ++i) {
    std::string line;
    std::string nname;
    uint64_t ninode;
    if (std::getline(f, line)) {
      std::istringstream ss(line);
      ss >> ninode;
      ss >> nname;
      pq.emplace(ninode, nname);
    } else {
      break;
    }
  }
  while (!pq.empty()) {
    auto p = pq.top();
    pq.pop();

    std::string line;
    std::string nname;
    uint64_t ninode;
    if (std::getline(f, line)) {
      std::istringstream ss(line);
      ss >> ninode;
      ss >> nname;
      pq.emplace(ninode, nname);
    }

    auto inode = p.first;
    auto name = p.second;
    if (!name.empty())
      t.Insert(inode, pr.GetPrefix(name));
  }

  auto prss = GetPeakRSS();
  for (const auto& x : t.map_) {
    std::cout << x.first << "-" << x.second.first << " "
              << FmtSet(t.Get(x.first)) << std::endl;
  }

  std::cout << "Elems " << t.map_.size() << std::endl;
  std::cout << orss << std::endl;
  std::cout << prss << std::endl;

  std::ifstream f2(input_file);
  size_t ninodes = 0;
  size_t wronginodes = 0;
  std::string line;
  while (std::getline(f2, line)) {
    ninodes++;
    std::istringstream ss(line);
    uint64_t inode;
    std::string name;
    ss >> inode;
    ss >> name;
    if (inode == 0)
      continue;
    std::set<std::string> found = t.Get(inode);
    bool seen = false;
    for (const std::string& s : found) {
      if (name.find(s) == 0)
        seen = true;
    }

    if (!seen) {
      std::cout << inode << std::endl;
      std::cout << name << std::endl;
      std::cout << FmtSet(found) << std::endl;
      ++wronginodes;
    }
  }

  std::cout << ninodes << " / " << wronginodes << std::endl;
  return 0;
}
}  // namespace perfetto

int main(int argc, char** argv) {
  perfetto::IOTracingTestMain2(argc, argv);
}
