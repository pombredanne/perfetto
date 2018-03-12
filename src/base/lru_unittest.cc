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

#include "perfetto/base/lru.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <string>
#include <tuple>

namespace perfetto {
namespace base {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::Pointee;

std::pair<int64_t, int64_t> key1{0, 0};
std::pair<int64_t, int64_t> key2{0, 1};
std::pair<int64_t, int64_t> key3{0, 2};

const char* val1 = "foo";
const char* val2 = "bar";
const char* val3 = "baz";

TEST(LRUTest, Basic) {
  LRUInodeCache cache(2);
  cache.Insert(key1, val1);
  EXPECT_THAT(cache.Get(key1), Pointee(Eq(val1)));
  cache.Insert(key2, val2);
  EXPECT_THAT(cache.Get(key1), Pointee(Eq(val1)));
  EXPECT_THAT(cache.Get(key2), Pointee(Eq(val2)));
}

TEST(LRUTest, Overflow) {
  LRUInodeCache cache(2);
  cache.Insert(key1, val1);
  cache.Insert(key2, val2);
  cache.Get(key1);
  cache.Get(key2);
  cache.Insert(key3, val3);
  // foo is the LRU and should be evicted.
  EXPECT_THAT(cache.Get(key1), IsNull());
  EXPECT_THAT(cache.Get(key2), Pointee(Eq(val2)));
  EXPECT_THAT(cache.Get(key3), Pointee(Eq(val3)));
}

}  // namespace
}  // namespace base
}  // namespace perfetto
