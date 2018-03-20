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
#include "perfetto/base/small_set.h"
#include "perfetto/base/string_splitter.h"
#include "src/traced/probes/filesystem/inode_file_data_source.h"
#include "src/traced/probes/filesystem/prefix_finder.h"

#ifndef SRC_TRACED_PROBES_FILESYSTEM_RANGE_TREE_H_
#define SRC_TRACED_PROBES_FILESYSTEM_RANGE_TREE_H_

// RANGES ARE [x, y), left-inclusive and right-exlusive.
namespace perfetto {
namespace {

constexpr size_t kSetSize = 3;
}  // namespace

class RangeTree {
 public:
  using DataType = PrefixFinder::Node*;

  const std::set<std::string> Get(Inode inode);
  void Insert(Inode inode, DataType interned);

 private:
  std::map<Inode, SmallSet<PrefixFinder::Node*, kSetSize>> map_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FILESYSTEM_RANGE_TREE_H_
