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

#include "gtest/gtest.h"

#include <thread>

#include "src/profiling/memory/client.h"  // For PThreadKey.

namespace perfetto {
namespace {

// Get integer that is large enough to be sampled in the first call to
// SampleSize. Emulates the interal behaviour of the RNG in
// ThreadLocalSamplingData.
uint64_t GetSmall() {
  std::default_random_engine random_engine(
      ThreadLocalSamplingData::seed_for_testing);
  std::exponential_distribution<double> dist(1 / 512.);
  return static_cast<uint64_t>(dist(random_engine)) + 1;
}

TEST(SamplerTest, TestLarge) {
  PThreadKey key(ThreadLocalSamplingData::KeyDestructor);
  ASSERT_TRUE(key.valid());
  EXPECT_EQ(SampleSize(key.get(), 1024, 512, malloc, free), 1024);
}

TEST(SamplerTest, TestSmall) {
  PThreadKey key(ThreadLocalSamplingData::KeyDestructor);
  ASSERT_TRUE(key.valid());
  uint64_t small = GetSmall();
  EXPECT_EQ(SampleSize(key.get(), small, 512, malloc, free), 512);
}

TEST(SamplerTest, TestSmallFromThread) {
  PThreadKey key(ThreadLocalSamplingData::KeyDestructor);
  ASSERT_TRUE(key.valid());
  uint64_t small = GetSmall();
  std::thread th([&key, small] {
    EXPECT_EQ(SampleSize(key.get(), small, 512, malloc, free), 512);
  });
  std::thread th2([&key, small] {
    // The threads should have separate state.
    EXPECT_EQ(SampleSize(key.get(), small, 512, malloc, free), 512);
  });
  th.join();
  th2.join();
}

}  // namespace
}  // namespace perfetto
