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

#include "src/profiling/memory/sampler.h"

#include "perfetto/base/utils.h"

namespace perfetto {
namespace profiling {

// The algorithm below is inspired by the Chromium sampling algorithm at
// https://cs.chromium.org/search/?q=f:cc+symbol:AllocatorShimLogAlloc+package:%5Echromium$&type=cs

int64_t Sampler::NextSampleInterval() {
  std::exponential_distribution<double> dist(sampling_rate_);
  int64_t next = static_cast<int64_t>(dist(random_engine_));
  // The +1 corrects the distribution of the first value in the interval.
  // TODO(fmayer): Figure out why.
  return next + 1;
}

size_t Sampler::NumberOfSamples(size_t alloc_sz) {
  interval_to_next_sample_ -= alloc_sz;
  size_t num_samples = 0;
  while (PERFETTO_UNLIKELY(interval_to_next_sample_ <= 0)) {
    interval_to_next_sample_ += NextSampleInterval();
    ++num_samples;
  }
  return num_samples;
}

size_t Sampler::SampleSize(size_t alloc_sz) {
  if (PERFETTO_UNLIKELY(alloc_sz >= sampling_interval_))
    return alloc_sz;
  return sampling_interval_ * NumberOfSamples(alloc_sz);
}

}  // namespace profiling
}  // namespace perfetto
