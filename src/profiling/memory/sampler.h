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

#ifndef SRC_PROFILING_MEMORY_SAMPLER_H_
#define SRC_PROFILING_MEMORY_SAMPLER_H_

#include <pthread.h>
#include <stdint.h>

#include <random>

namespace perfetto {

class ThreadLocalSamplingData {
 public:
  size_t ShouldSample(size_t sz, double rate);

 private:
  int64_t NextSampleInterval(double rate);
  int64_t interval_to_next_sample_ = 0;
  std::default_random_engine random_engine_;
};

size_t ShouldSample(pthread_key_t key, size_t sz, double rate);
void KeyDestructor(void* ptr);

}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_SAMPLER_H_
