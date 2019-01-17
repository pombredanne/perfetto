/*
 * Copyright (C) 2019 The Android Open Source Project
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

  ASSERT_STREQ(TaskState::From(0).ToString().data(), "R");
  ASSERT_STREQ(TaskState::From(1).ToString().data(), "S");
  ASSERT_STREQ(TaskState::From(2).ToString().data(), "D");
  ASSERT_STREQ(TaskState::From(4).ToString().data(), "T");
  ASSERT_STREQ(TaskState::From(8).ToString().data(), "t");
  ASSERT_STREQ(TaskState::From(16).ToString().data(), "X");
  ASSERT_STREQ(TaskState::From(32).ToString().data(), "Z");
  ASSERT_STREQ(TaskState::From(64).ToString().data(), "x");
  ASSERT_STREQ(TaskState::From(128).ToString().data(), "K");
  ASSERT_STREQ(TaskState::From(256).ToString().data(), "W");
  ASSERT_STREQ(TaskState::From(512).ToString().data(), "P");
  ASSERT_STREQ(TaskState::From(1024).ToString().data(), "N");
}

TEST(TaskStateUnittest, MultipleState) {
  ASSERT_STREQ(TaskState::From(2048).ToString().data(), "R+");
  ASSERT_STREQ(TaskState::From(130).ToString().data(), "DK");
  ASSERT_STREQ(TaskState::From(258).ToString().data(), "DW");
  ASSERT_STREQ(TaskState::From(1184).ToString().data(), "ZKN");
}

}  // namespace
}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto
