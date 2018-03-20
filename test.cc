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

constexpr size_t kMaxScans = 50000;
constexpr Inode kMergeDistance = 100;
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

  bool CanMergeSets(const SmallSet a, const SmallSet b, DataType y) {
    SmallSet c;
    for (const auto x : a) {
      if (!c.Add(x))
        return false;
    }
    for (const auto x : b) {
      if (!c.Add(x))
        return false;
    }
    if (!c.Add(y))
      return false;
    return true;
  }

  void Insert(Inode inode, DataType interned) {
    const auto surr = GetSurrounding(inode);
    const auto& lower = surr.first;
    const auto& upper = surr.second;

    // Inserting in-between two ranges that can be merged.
    if (ConsiderLower(lower, upper) && ConsiderUpper(lower, upper) &&
        CanMergeSets(lower->second.second, upper->second.second, interned)) {
      for (const DataType x : upper->second.second) {
        AddToSet(&lower->second.second, x);
      }
      AddToSet(&lower->second.second, interned);
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

    auto merge_strategy = GetMergeStrategy(lower, upper, inode, interned);
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
      case SurroundingRanges::kNone: {
        SmallSet set;
        set.Add(interned);
        CheckEmplace(map_, inode, std::make_pair(inode + 1, std::move(set)));
        break;
      }
      case SurroundingRanges::kLower:
#if PERFETTO_DCHECK_IS_ON()
        PERFETTO_DCHECK(lower == nsurr.first);
#endif
        lower->second.first = std::max(lower->second.first, inode + 1);
        AddToSet(&lower->second.second, interned);
        break;
      case SurroundingRanges::kUpper:
#if PERFETTO_DCHECK_IS_ON()
        PERFETTO_DCHECK(upper == nsurr.second);
#endif
        AddToSet(&upper->second.second, interned);
        if (inode != upper->first) {
          CheckEmplace(map_, inode, upper->second);
          map_.erase(upper);
        }
        break;
    }
  }

 private:
  using MapType = std::map<Inode, std::pair<Inode, SmallSet>>;
  MapType map_;

  enum class SurroundingRanges {
    kNone = 0,
    kLower = 1,
    kUpper = 2,
  };

  // Whether the lower range of the surrounding ranges is valid.
  // It is invalid if the upper range is the first element in the map.
  bool ConsiderLower(const MapType::iterator& lower,
                     const MapType::iterator& upper) {
    return upper != lower;
  }
  // Whether the upper range of the surrounding ranges is valid.
  // It is invalid iff the map is empty.
  bool ConsiderUpper(const MapType::iterator& lower,
                     const MapType::iterator& upper) {
    return upper != map_.cend();
  }

  bool IsInSet(const SmallSet& s, DataType e) { return s.Contains(e); }

  void AddToSet(SmallSet* s, DataType e) {
    if (IsInSet(*s, e)) {
      return;
    }
    PERFETTO_DCHECK(s->size() < kSetSize);
    s->Add(e);
    return;
  }

  // Return whether inode overlaps with lower, upper or no range.
  SurroundingRanges GetOverlap(const MapType::iterator& lower,
                               const MapType::iterator& upper,
                               Inode inode) {
    if (upper != map_.end() && inode == upper->first)
      return SurroundingRanges::kUpper;
    if (ConsiderLower(lower, upper) && inode < lower->second.first)
      return SurroundingRanges::kLower;
    return SurroundingRanges::kNone;
  }

  // Return whether inode with value should be merged with lower or upper
  // range. Returns kUpper, kLower or kNone if it should not be merged.
  // Always prefer merging with lower as that is cheaper to do.
  SurroundingRanges GetMergeStrategy(const MapType::iterator& lower,
                                     const MapType::iterator& upper,
                                     Inode inode,
                                     DataType value) {
    // Sanity check passed lower / upper ranges.
    PERFETTO_DCHECK(
        (!ConsiderLower(lower, upper) || inode >= lower->first) &&
        (!ConsiderUpper(lower, upper) || inode < upper->second.first));

    // We cannot merge with the upper range if we overlap with the lower.
    auto overlap = GetOverlap(lower, upper, inode);
    bool consider_upper = ConsiderUpper(lower, upper);
    if (overlap == SurroundingRanges::kLower)
      consider_upper = false;

    // Only merge up to kMergeDistance in order to prevent huge ranges.
    // In theory, if the first and the last element come after each other,
    // they will be merged into a range spanning the entire space, which will
    // have to be split for every insert. Needless to say, this would be bad.
    PERFETTO_DCHECK(!consider_upper || upper->first >= inode);
    if (consider_upper && kMergeDistance != 0 &&
        (upper->first - inode) > kMergeDistance)
      consider_upper = false;

    bool consider_lower = ConsiderLower(lower, upper);
    if (consider_lower && kMergeDistance != 0 && inode > lower->second.first &&
        (inode - lower->second.first) > kMergeDistance)
      consider_lower = false;

    // If the folder is already contained in either of them, merge into that.
    // Prefer lower.
    if (consider_lower && IsInSet(lower->second.second, value))
      return SurroundingRanges::kLower;
    if (consider_upper && IsInSet(upper->second.second, value))
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

  std::pair<MapType::iterator, MapType::iterator> GetSurrounding(Inode inode) {
    auto upper = map_.lower_bound(inode);
    auto lower = upper;
    if (upper != map_.begin()) {
      lower--;
    }
    PERFETTO_DCHECK(!ConsiderLower(lower, upper) || lower != upper);
    return {lower, upper};
  }
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
  ScanFilesDFS("/data", [&pr](BlockDeviceID, Inode i, std::string name,
                              protos::pbzero::InodeFileMap_Entry_Type) {
    pr.AddPath(name);
  });

  RangeTree t;
  ScanFilesDFS("/data", [&t, &pr](BlockDeviceID, Inode i, std::string name,
                                  protos::pbzero::InodeFileMap_Entry_Type) {
    t.Insert(i, pr.GetPrefix(name));
  });

  std::string out;
  PERFETTO_CHECK(base::ReadFile(
      std::string("/proc/") + std::to_string(getpid()) + "/smaps_rollup",
      &out));
  std::cout << out << std::endl;

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

}  // namespace perfetto

int main(int argc, char** argv) {
  perfetto::IOTracingTestMain2(argc, argv);
}
