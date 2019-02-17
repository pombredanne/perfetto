/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_BASE_CIRCULAR_QUEUE_H_
#define INCLUDE_PERFETTO_BASE_CIRCULAR_QUEUE_H_

#include <stdint.h>
#include <string.h>
#include <iterator>

#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"

namespace perfetto {
namespace base {

// CircularQueue is a push-back, pop-front queue with the following
// characteristics:
// - The storage is based on a flat circular buffer. Beginning and end wrap
//   as necessary, to keep pushes and pops O(1) as long as capacity expansion is
//   not required.
// - Capacity is automatically expanded as in a std::vector. Expansion has a
//   O(N) cost.
// - It allows random access, allowing in-place std::sort.
// - Iterators are not stable. Mutating the container invalidates all iterators.
// - It doesn't bother with const-correctness.
//
// Implementation details:
// Internally, |begin|, |end| and iterators use 64-bit monotonic indexes, which
// are incremented as if the queue was backed on unlimited storage.
// Even assuming that elements are inserted and removed every ns, 64 bit is
// enough for 584 years.
// Wrapping happens only when addressing elements in the underlying circular
// storage. This limits the complexity and avoiding dealing with modular
// arithmetic all over the places.
template <class T>
class CircularQueue {
 public:
  class Iterator {
   public:
    using difference_type = ptrdiff_t;
    using value_type = T;
    using pointer = const T*;
    using reference = const T&;
    using iterator_category = std::random_access_iterator_tag;

    Iterator(CircularQueue* queue, uint64_t pos, uint32_t generation)
        : queue_(queue),
          pos_(pos)
#if PERFETTO_DCHECK_IS_ON()
          ,
          generation_(generation)
#endif
    {
      ignore_result(generation);
    }

    T* operator->() {
#if PERFETTO_DCHECK_IS_ON()
      PERFETTO_DCHECK(generation_ == queue_->generation());
#endif
      return queue_->Get(pos_);
    }
    T& operator*() { return *(operator->()); }

    const value_type& operator[](difference_type i) const {
      return *(*this + i);
    }

    Iterator& operator++() {
      Add(1);
      return *this;
    }

    Iterator operator++(int) {
      Iterator ret = *this;
      Add(1);
      return ret;
    }

    Iterator& operator--() {
      Add(-1);
      return *this;
    }

    Iterator operator--(int) {
      Iterator ret = *this;
      Add(-1);
      return ret;
    }

    friend Iterator operator+(const Iterator& iter, difference_type offset) {
      Iterator ret = iter;
      ret.Add(offset);
      return ret;
    }

    Iterator& operator+=(difference_type offset) {
      Add(offset);
      return *this;
    }

    friend Iterator operator-(const Iterator& iter, difference_type offset) {
      Iterator ret = iter;
      ret.Add(-offset);
      return ret;
    }

    Iterator& operator-=(difference_type offset) {
      Add(-offset);
      return *this;
    }

    friend ptrdiff_t operator-(const Iterator& lhs, const Iterator& rhs) {
      return static_cast<ptrdiff_t>(lhs.pos_) -
             static_cast<ptrdiff_t>(rhs.pos_);
    }

    friend bool operator==(const Iterator& lhs, const Iterator& rhs) {
      return lhs.pos_ == rhs.pos_;
    }

    friend bool operator!=(const Iterator& lhs, const Iterator& rhs) {
      return lhs.pos_ != rhs.pos_;
    }

    friend bool operator<(const Iterator& lhs, const Iterator& rhs) {
      return lhs.pos_ < rhs.pos_;
    }

    friend bool operator<=(const Iterator& lhs, const Iterator& rhs) {
      return lhs.pos_ <= rhs.pos_;
    }

    friend bool operator>(const Iterator& lhs, const Iterator& rhs) {
      return lhs.pos_ > rhs.pos_;
    }

    friend bool operator>=(const Iterator& lhs, const Iterator& rhs) {
      return lhs.pos_ >= rhs.pos_;
    }

   private:
    inline void Add(difference_type offset) {
      pos_ = static_cast<uint64_t>(static_cast<difference_type>(pos_) + offset);
      PERFETTO_DCHECK(pos_ <= queue_->end_);
    }

    CircularQueue* queue_;
    uint64_t pos_;

#if PERFETTO_DCHECK_IS_ON()
    uint32_t generation_;
#endif
  };

  CircularQueue(size_t initial_capacity = 1024) {
    PERFETTO_CHECK((initial_capacity & (initial_capacity - 1)) == 0);
    capacity_ = initial_capacity;
    vec_ = static_cast<T*>(malloc(capacity_ * sizeof(T)));
  }

  ~CircularQueue() {
    erase_front(size());
    PERFETTO_DCHECK(empty());
    free(vec_);
  }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    increment_generation();
    if (PERFETTO_UNLIKELY(size() == capacity_))
      Grow();
    T* slot = Get(end_++);
    new (slot) T(std::forward<Args>(args)...);
  }

  void erase_front(size_t n) {
    increment_generation();
    for (; n && (begin_ < end_); --n) {
      Get(begin_)->~T();
      begin_++;  // This needs to be its own statement, Get() checks begin_.
    }
  }

  void pop_front() { erase_front(1); }

  T& at(size_t idx) {
    PERFETTO_DCHECK(idx < size());
    return *Get(begin_ + idx);
  }

  Iterator begin() { return Iterator(this, begin_, generation()); }
  Iterator end() { return Iterator(this, end_, generation()); }
  T& front() { return *begin(); }
  T& back() { return *(end() - 1); }

  bool empty() const { return size() == 0; }

  size_t size() const {
    PERFETTO_DCHECK(end_ - begin_ <= static_cast<uint64_t>(capacity_));
    return static_cast<size_t>(end_ - begin_);
  }

  size_t capacity() const { return capacity_; }

#if PERFETTO_DCHECK_IS_ON()
  uint32_t generation() const { return generation_; }
  void increment_generation() { ++generation_; }
#else
  uint32_t generation() const { return 0; }
  void increment_generation() {}
#endif

 private:
  void Grow() {
    // Capacity must be always a power of two. This allows Get() to use a simple
    // bitwise-AND for handling the wrapping instead of a full division.
    size_t new_capacity = static_cast<size_t>(capacity_ * 2);
    PERFETTO_CHECK(new_capacity > capacity_);  // Hit the 4GB wall on 32-bit.
    auto new_vec = static_cast<T*>(malloc(new_capacity * sizeof(T)));

    // Move all elements in the expanded array.
    size_t new_size = 0;
    for (uint64_t i = begin_; i < end_; i++)
      new (&new_vec[new_size++]) T(std::move(*Get(i)));  // Placement move ctor.

    // Even if all the elements are std::move()-d and likely empty, we are still
    // required to call the dtor for them.
    for (uint64_t i = begin_; i < end_; i++)
      Get(i)->~T();
    free(vec_);

    begin_ = 0;
    end_ = new_size;
    capacity_ = new_capacity;
    vec_ = new_vec;
  }

  inline T* Get(uint64_t pos) {
    PERFETTO_DCHECK(pos >= begin_ && pos < end_);
    PERFETTO_DCHECK((capacity_ & (capacity_ - 1)) == 0);
    auto mod = static_cast<size_t>(pos & (capacity_ - 1));
    return &vec_[mod];
  }

  T* vec_;
  uint64_t begin_ = 0;
  uint64_t end_ = 0;
  uint64_t capacity_;
#if PERFETTO_DCHECK_IS_ON()
  uint32_t generation_ = 0;
#endif
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_CIRCULAR_QUEUE_H_
