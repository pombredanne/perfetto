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

#ifndef INCLUDE_PERFETTO_BASE_LRU_H_
#define INCLUDE_PERFETTO_BASE_LRU_H_

#include <list>
#include <map>
#include <string>
#include <tuple>

namespace perfetto {
namespace base {

using InodeKey = std::pair<int64_t, int64_t>;
using InodeValue = std::string;

// LRUInodeCache keeps up to capacity entries in a mapping from InodeKey
// to InodeValue. This is used to map <block device, inode> tuples to file
// paths.
class LRUInodeCache {
 public:
  explicit LRUInodeCache(size_t capacity) : capacity_(capacity) {}

  const InodeValue* Get(const InodeKey& k);
  void Insert(const InodeKey k, const InodeValue v);

 private:
  using ItemType = std::pair<const InodeKey, const InodeValue>;
  using ListIteratorType = typename std::list<ItemType>::iterator;
  using MapType = std::map<const InodeKey, ListIteratorType>;

  void Insert(typename MapType::iterator map_it,
              const InodeKey k,
              const InodeValue v);

  size_t capacity_;
  MapType map_;
  std::list<ItemType> list_;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_LRU_H_
