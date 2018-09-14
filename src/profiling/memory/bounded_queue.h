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

#ifndef SRC_PROFILING_MEMORY_BOUNDED_QUEUE_H_
#define SRC_PROFILING_MEMORY_BOUNDED_QUEUE_H_

#include <condition_variable>
#include <deque>
#include <mutex>

template <typename T>
class BoundedQueue {
 public:
  BoundedQueue() : BoundedQueue(1) {}
  BoundedQueue(size_t size) : size_(size) {}

  bool Add(T item) {
    std::unique_lock<std::mutex> l(mtx_);
    if (deque_.size() == size_)
      full_cv_.wait(l, [this] { return deque_.size() < size_; });
    deque_.emplace_back(std::move(item));
    if (deque_.size() == 1)
      empty_cv_.notify_one();
    return true;
  }

  T Get() {
    std::unique_lock<std::mutex> l(mtx_);
    if (elements_ == 0)
      empty_cv_.wait(l, [this] { return deque_.size() > 0; });
    T item(std::move(deque_.front()));
    deque_.pop_front();
    if (deque_.size() == size_ - 1)
      full_cv_.notify_one();
    return item;
  }

  void SetSize(size_t size) {
    std::lock_guard<std::mutex> l(mtx_);
    size_ = size;
  }

 private:
  size_t size_;

  size_t elements_ = 0;
  std::mutex mtx_;
  std::condition_variable full_cv_;
  std::condition_variable empty_cv_;
  std::deque<T> deque_;
};

#endif  // SRC_PROFILING_MEMORY_BOUNDED_QUEUE_H_
