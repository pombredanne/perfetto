/*
 * Copyright (C) 2017 The Android Open Source Project
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

namespace perfetto {
namespace base {

namespace {
template <typename T>
class PtrLess {
 public:
  bool operator()(T* lhs, T* rhs) const { return lhs != rhs && *lhs < *rhs; }
};
}  // namespace

/*
 * LRUCache is a memory efficient LRU map.
 *
 * It does not store the value of the key twice but rather
 * stores a pointer in the key of the map.
 */
template <typename K, typename V>
class LRUCache {
 public:
  explicit LRUCache(size_t capacity) : capacity_(capacity) {}

  V* get(K k) {
    const auto& it = item_map_.find(&k);
    if (it == item_map_.end()) {
      return nullptr;
    }
    auto list_entry = it->second;
    // Bump this item to the front of the cache.
    insert(list_entry->first, list_entry->second);
    return &(list_entry->second);
  }

  void insert(K k, V v) {
    auto existing = item_map_.find(&k);
    if (existing != item_map_.end()) {
      // This MUST happen before the following line to avoid
      // a dangling pointer.
      item_map_.erase(existing);
      item_list_.erase(existing->second);
    }

    if (item_map_.size() == capacity_) {
      auto last_elem = item_list_.end();
      last_elem--;
      item_map_.erase(&(last_elem->first));
      item_list_.erase(last_elem);
    }
    item_list_.emplace_front(k, v);
    item_map_.emplace(&(item_list_.begin()->first), item_list_.begin());
  }

 private:
  size_t capacity_;
  std::map<K*, typename std::list<std::pair<K, V>>::iterator, PtrLess<K>>
      item_map_;
  std::list<std::pair<K, V>> item_list_;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_LRU_H_
