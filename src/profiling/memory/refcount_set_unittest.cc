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

#include "src/profiling/memory/refcount_set.h"

#include "gtest/gtest.h"
#include "perfetto/base/logging.h"

namespace perfetto {
namespace profiling {
namespace {

size_t instances = 0;

class TestData {
 public:
  TestData(const TestData&) = delete;
  TestData& operator=(const TestData&) = delete;
  TestData(const TestData&&) = delete;
  TestData& operator=(const TestData&&) = delete;

  TestData(int d) : data(d) { instances++; }

  ~TestData() { instances--; }

  bool operator<(const TestData& other) const { return data < other.data; }

 private:
  int data;
};

TEST(RefcountSetTest, Basic) {
  RefcountSet<int> s;
  auto handle = s.Emplace(1);
  ASSERT_EQ(*handle, 1);
}

TEST(RefcountSetTest, OnlyOne) {
  RefcountSet<TestData> s;
  {
    auto handle = s.Emplace(1);
    ASSERT_EQ(instances, 1);
    auto handle2 = s.Emplace(1);
    ASSERT_EQ(instances, 1);
  }
  ASSERT_EQ(instances, 0);
}

TEST(RefcountSetTest, Two) {
  {
    RefcountSet<TestData> s;
    auto handle = s.Emplace(1);
    ASSERT_EQ(instances, 1);
    auto handle2 = s.Emplace(1);
    ASSERT_EQ(instances, 1);
    {
      auto handle3 = s.Emplace(2);
      ASSERT_EQ(instances, 2);
    }
    ASSERT_EQ(instances, 1);
  }
  ASSERT_EQ(instances, 0);
}

}  // namespace
}  // namespace profiling
}  // namespace perfetto
