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

#include "perfetto/base/string_writer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace base {
namespace {

TEST(StringWriterTest, BasicCases) {
  char buffer[128];
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WriteChar('0');
    ASSERT_STREQ(writer.GetCString(), "0");
  }
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WriteInt(132545);
    ASSERT_STREQ(writer.GetCString(), "132545");
  }
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WritePaddedInt<'0', 3>(0);
    ASSERT_STREQ(writer.GetCString(), "000");
  }
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WritePaddedInt<'0', 1>(1);
    ASSERT_STREQ(writer.GetCString(), "1");
  }
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WritePaddedInt<'0', 3>(1);
    ASSERT_STREQ(writer.GetCString(), "001");
  }
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WritePaddedInt<'0', 0>(1);
    ASSERT_STREQ(writer.GetCString(), "1");
  }
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WritePaddedInt<' ', 5>(123);
    ASSERT_STREQ(writer.GetCString(), "  123");
  }
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WriteDouble(123.25);
    ASSERT_STREQ(writer.GetCString(), "123.250000");
  }

  constexpr char kTestStr[] = "test";
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WriteString(kTestStr, sizeof(kTestStr) - 1);
    ASSERT_STREQ(writer.GetCString(), kTestStr);
  }
  {
    base::StringWriter writer(buffer, sizeof(buffer));
    writer.WriteString(kTestStr);
    ASSERT_STREQ(writer.GetCString(), kTestStr);
  }
}

TEST(StringWriterTest, WriteAllTypes) {
  char buffer[128];
  base::StringWriter writer(buffer, sizeof(buffer));
  writer.WriteChar('0');
  writer.WriteInt(132545);
  writer.WritePaddedInt<'0', 0>(1);
  writer.WritePaddedInt<'0', 3>(0);
  writer.WritePaddedInt<'0', 1>(1);
  writer.WritePaddedInt<'0', 2>(1);
  writer.WritePaddedInt<'0', 3>(1);
  writer.WritePaddedInt<' ', 5>(123);
  writer.WriteDouble(123.25);

  constexpr char kTestStr[] = "test";
  writer.WriteString(kTestStr, sizeof(kTestStr) - 1);
  writer.WriteString(kTestStr);

  ASSERT_STREQ(writer.GetCString(), "01325451000101001  123123.250000testtest");
}

}  // namespace
}  // namespace base
}  // namespace perfetto
