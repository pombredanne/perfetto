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

#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

TEST(StringPoolTest, InternAndRetrieve) {
  StringPool pool;

  static char kString[] = "Test String";
  auto id = pool.InternString(kString);
  ASSERT_STREQ(pool.Get(id).data(), kString);
  ASSERT_EQ(pool.Get(id), kString);
}

TEST(StringPoolTest, InternTwiceGivesSameId) {
  StringPool pool;

  static char kString[] = "Test String";
  auto first_id = pool.InternString(kString);
  ASSERT_EQ(first_id, pool.InternString(kString));
}

TEST(StringPoolTest, NullPointerHandling) {
  StringPool pool;

  auto id = pool.InternString(nullptr);
  ASSERT_EQ(pool.Get(id).data(), nullptr);
}

TEST(StringPoolTest, Iterator) {
  StringPool pool;

  auto it = pool.CreateIterator();
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.StringView().data(), nullptr);
  ASSERT_FALSE(it.Next());

  static char kString[] = "Test String";
  pool.InternString(kString);

  it = pool.CreateIterator();
  ASSERT_TRUE(it.Next());
  ASSERT_TRUE(it.Next());
  ASSERT_STREQ(it.StringView().data(), kString);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
