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

#ifndef SRC_PROFILING_MEMORY_RESETTING_RAW_PTR_H_
#define SRC_PROFILING_MEMORY_RESETTING_RAW_PTR_H_

namespace perfetto {
namespace profiling {

// Non-owning pointer that resets to nullptr when being moved out of.
template <typename T>
class ResettingRawPtr {
 public:
  explicit ResettingRawPtr(T* ptr) : ptr_(ptr) {}
  ResettingRawPtr(const ResettingRawPtr& other) : ptr_(other.ptr_) {}
  ResettingRawPtr& operator=(const ResettingRawPtr& other) {
    ptr_ = other.ptr_;
    return *this;
  }

  ResettingRawPtr(ResettingRawPtr&& other) {
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
  }

  ResettingRawPtr& operator=(ResettingRawPtr&& other) {
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
    return *this;
  }

  T* operator*() { return ptr_; }

  T* operator*() const { return ptr_; }

  T* operator->() { return ptr_; }

  const T* operator->() const { return ptr_; }

  operator bool() const { return ptr_ != nullptr; }

  const T* get() const { return ptr_; }

  T* get() { return ptr_; }

 private:
  T* ptr_;
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_RESETTING_RAW_PTR_H_
