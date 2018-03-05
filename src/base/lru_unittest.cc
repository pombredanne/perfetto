/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "gtest/gtest.h"

#include <string>

namespace perfetto {
namespace base {
namespace {

TEST(LRUTest, Basic) {
  LRUCache<std::string, std::string> cache(2);
  cache.insert("foo", "bar");
  EXPECT_EQ(*cache.get("foo"), "bar");
  cache.insert("qux", "asd");
  EXPECT_EQ(*cache.get("foo"), "bar");
  EXPECT_EQ(*cache.get("qux"), "asd");
}

TEST(LRUTest, Overflow) {
  LRUCache<std::string, std::string> cache(2);
  cache.insert("foo", "bar");
  cache.insert("qux", "asd");
  cache.get("foo");
  cache.get("qux");
  cache.insert("spam", "eggs");
  // foo is the LRU and should be evicted.
  EXPECT_EQ(cache.get("foo"), nullptr);
  EXPECT_EQ(*cache.get("qux"), "asd");
  EXPECT_EQ(*cache.get("spam"), "eggs");
}

}  // namespace
}  // namespace base
}  // namespace perfetto
