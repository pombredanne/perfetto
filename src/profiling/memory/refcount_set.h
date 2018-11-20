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

#ifndef SRC_PROFILING_MEMORY_REFCOUNT_SET_H_
#define SRC_PROFILING_MEMORY_REFCOUNT_SET_H_

#include <set>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace profiling {

template <typename T>
class RefcountSetHandle;

template <typename T>
class RefcountSet {
 public:
  friend class RefcountSetHandle<T>;
  template <typename... U>
  RefcountSetHandle<T> Emplace(U... args);
  ~RefcountSet() {
    if (!data_.empty())
      PERFETTO_DFATAL("Destroying RefcountSet with active handles.");
  }

 private:
  struct Entry {
    template <typename... U>
    Entry(U... args) : data(std::forward<U...>(args...)) {}

    bool operator<(const Entry& other) const { return data < other.data; }

    const T data;
    uint64_t refcount = 0;
  };

  std::set<Entry> data_;
};

template <typename T>
class RefcountSetHandle {
 public:
  RefcountSetHandle(
      typename std::set<typename RefcountSet<T>::Entry>::iterator iterator,
      RefcountSet<T>* set)
      : iterator_(iterator), set_(set) {}

  T* operator->() { return const_cast<T*>(&(*iterator_->data)); }

  T& operator*() { return const_cast<T&>(iterator_->data); }

  ~RefcountSetHandle() {
    const typename RefcountSet<T>::Entry& entry = *iterator_;
    if (--const_cast<typename RefcountSet<T>::Entry&>(entry).refcount == 0)
      set_->data_.erase(iterator_);
  }

 private:
  typename std::set<typename RefcountSet<T>::Entry>::iterator iterator_;
  RefcountSet<T>* set_;
};

template <typename T>
template <typename... U>
RefcountSetHandle<T> RefcountSet<T>::Emplace(U... args) {
  typename decltype(data_)::iterator it;
  std::tie(it, std::ignore) = data_.emplace(std::forward<U...>(args...));
  const Entry& entry = *it;
  const_cast<Entry&>(entry).refcount++;
  return {it, this};
}

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_REFCOUNT_SET_H_
