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

#ifndef SRC_PROFILING_MEMORY_CLIENT_H_
#define SRC_PROFILING_MEMORY_CLIENT_H_

#include <stddef.h>
#include <mutex>
#include <vector>

namespace perfetto {

class FreePage {
 public:
  FreePage();

  void Add(const void* addr, int fd);
  bool Flush(int fd);

 private:
  std::vector<uint64_t> free_page_;
  std::mutex mtx_;
  size_t offset_;
};

class Client {
 public:
  void LogMalloc(const void* pointer, size_t size);
  void LogFree(const void* pointer);

 private:
  FreePage free_page;
};

}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_CLIENT_H_
