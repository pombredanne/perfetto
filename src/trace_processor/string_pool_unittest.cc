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

#include "src/trace_processor/string_pool.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

TEST(StringPoolTest, Deduplication) {
  StringPool sp;

  StringId id1 = sp.Insert("");
  EXPECT_EQ(sp.Get(id1), "");
  EXPECT_EQ(sp.size(), 1);

  StringId id2 = sp.Insert("x");
  EXPECT_NE(id1, id2);
  EXPECT_EQ(sp.Get(id2), "x");

  EXPECT_EQ(sp.Insert(""), id1);
  EXPECT_EQ(sp.size(), 2);

  // Insert a bunch of distinct strings.
  std::string str;
  str.reserve(64 * 1024);
  StringId last_id = id2;
  for (size_t i = 1; i <= 1024; i++) {
    str.assign(i, '!');
    StringId id = sp.Insert(base::StringView(str));
    ASSERT_GT(id, last_id);
    last_id = id;
  }

  // Inserting them a 2nd time shouln't add any new strings to the pool.
  for (size_t i = 1; i <= 1024; i++) {
    str.assign(i, '!');
    StringId id = sp.Insert(base::StringView(str));
    ASSERT_LE(id, last_id);
    ASSERT_EQ(sp.Get(id), str);
  }

  EXPECT_EQ(sp.size(), 1024 + 2);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
