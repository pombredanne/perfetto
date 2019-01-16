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

#include "src/trace_processor/ftrace_utils.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {
namespace {

using ::testing::ElementsAre;

TEST(TaskStateUnittest, Unknown) {
  TaskState state = TaskState::Unknown();
  ASSERT_FALSE(state.IsValid());
}

TEST(TaskStateUnittest, Smoke) {
  auto state = TaskState::From(0);
  ASSERT_TRUE(state.IsValid());

  char buffer[4];

  ASSERT_EQ(TaskState::From(0).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "R");

  ASSERT_EQ(TaskState::From(1).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "S");

  ASSERT_EQ(TaskState::From(2).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "D");

  ASSERT_EQ(TaskState::From(4).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "T");

  ASSERT_EQ(TaskState::From(8).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "t");

  ASSERT_EQ(TaskState::From(16).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "X");

  ASSERT_EQ(TaskState::From(32).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "Z");

  ASSERT_EQ(TaskState::From(64).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "x");

  ASSERT_EQ(TaskState::From(128).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "K");

  ASSERT_EQ(TaskState::From(256).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "W");

  ASSERT_EQ(TaskState::From(512).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "P");

  ASSERT_EQ(TaskState::From(1024).ToString(buffer, sizeof(buffer)), 1);
  ASSERT_STREQ(buffer, "N");
}

TEST(TaskStateUnittest, CommonMultipleState) {
  char buffer[3];

  ASSERT_EQ(TaskState::From(2048).ToString(buffer, sizeof(buffer)), 2);
  ASSERT_STREQ(buffer, "R+");

  char buffer2[4];

  ASSERT_EQ(TaskState::From(130).ToString(buffer2, sizeof(buffer2)), 3);
  ASSERT_STREQ(buffer2, "D|K");

  ASSERT_EQ(TaskState::From(258).ToString(buffer2, sizeof(buffer2)), 3);
  ASSERT_STREQ(buffer2, "D|W");
}

TEST(TaskStateUnittest, LargeStrings) {
  char buffer[22];

  ASSERT_EQ(TaskState::From(1184).ToString(buffer, sizeof(buffer)), 5);
  ASSERT_STREQ(buffer, "Z|K|N");

  ASSERT_EQ(TaskState::From(2044).ToString(buffer, sizeof(buffer)), 17);
  ASSERT_STREQ(buffer, "T|t|X|Z|x|K|W|P|N");

  ASSERT_EQ(TaskState::From(2046).ToString(buffer, sizeof(buffer)), 19);
  ASSERT_STREQ(buffer, "D|T|t|X|Z|x|K|W|P|N");

  ASSERT_EQ(TaskState::From(2047).ToString(buffer, sizeof(buffer)), 21);
  ASSERT_STREQ(buffer, "S|D|T|t|X|Z|x|K|W|P|N");
}

TEST(TaskStateUnittest, Overflow) {
  char buffer[2];

  ASSERT_EQ(TaskState::From(2048).ToString(buffer, sizeof(buffer)), 2);
  ASSERT_STREQ(buffer, "R");

  char buffer2[3];

  ASSERT_EQ(TaskState::From(1184).ToString(buffer2, sizeof(buffer2)), 5);
  ASSERT_STREQ(buffer2, "Z");

  ASSERT_EQ(TaskState::From(2044).ToString(buffer2, sizeof(buffer2)), 17);
  ASSERT_STREQ(buffer2, "T");
}

}  // namespace
}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto
