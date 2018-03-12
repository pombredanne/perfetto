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

#include "perfetto/base/lru.h"

namespace perfetto {
namespace base {

const InodeValue* LRUInodeCache::Get(const InodeKey& k) {
  const auto& it = map_.find(k);
  if (it == map_.end()) {
    return nullptr;
  }
  auto list_entry = it->second;
  // Bump this item to the front of the cache.
  // We can borrow the existing item's key and value because insert
  // does not care about it.
  Insert(it, std::move(list_entry->first), std::move(list_entry->second));
  return &list_.cbegin()->second;
}

void LRUInodeCache::Insert(const InodeKey k, const InodeValue v) {
  return Insert(map_.find(k), std::move(k), std::move(v));
}

void LRUInodeCache::Insert(typename MapType::iterator map_it,
                           const InodeKey k,
                           const InodeValue v) {
  list_.emplace_front(std::move(k), std::move(v));
  if (map_it != map_.end()) {
    ListIteratorType& list_it = map_it->second;
    list_.erase(list_it);
    list_it = list_.begin();
  } else {
    map_.emplace(k, list_.begin());
  }

  if (map_.size() > capacity_) {
    auto list_last_it = list_.end();
    list_last_it--;
    map_.erase(list_last_it->first);
    list_.erase(list_last_it);
  }
}
}  // namespace base
}  // namespace perfetto
