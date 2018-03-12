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

#include "perfetto/base/string_splitter.h"

#include "gtest/gtest.h"

namespace perfetto {
namespace base {
namespace {

TEST(StringSplitterTest, StdString) {
  EXPECT_EQ(nullptr, StringSplitter("", 'x').GetNextToken());
  EXPECT_EQ(nullptr, StringSplitter(std::string(), 'x').GetNextToken());
  {
    StringSplitter ss("a", 'x');
    EXPECT_STREQ("a", ss.GetNextToken());
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    StringSplitter ss("abc", 'x');
    EXPECT_STREQ("abc", ss.GetNextToken());
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    StringSplitter ss("ab,", ',');
    EXPECT_STREQ("ab", ss.GetNextToken());
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    StringSplitter ss(",ab,", ',');
    EXPECT_STREQ("ab", ss.GetNextToken());
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    StringSplitter ss("a,b,c", ',');
    EXPECT_STREQ("a", ss.GetNextToken());
    EXPECT_STREQ("b", ss.GetNextToken());
    EXPECT_STREQ("c", ss.GetNextToken());
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    StringSplitter ss("a,b,c,", ',');
    EXPECT_STREQ("a", ss.GetNextToken());
    EXPECT_STREQ("b", ss.GetNextToken());
    EXPECT_STREQ("c", ss.GetNextToken());
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    StringSplitter ss(",,a,,b,,,,c,", ',');
    EXPECT_STREQ("a", ss.GetNextToken());
    EXPECT_STREQ("b", ss.GetNextToken());
    EXPECT_STREQ("c", ss.GetNextToken());
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    StringSplitter ss(",,", ',');
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    StringSplitter ss(",,foo", ',');
    EXPECT_STREQ("foo", ss.GetNextToken());
  }
}

TEST(StringSplitterTest, CString) {
  {
    char buf[] = "\0x\0";
    StringSplitter ss(buf, sizeof(buf), ',');
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = "foo\nbar\n\nbaz\n";
    StringSplitter ss(buf, sizeof(buf), '\n');
    EXPECT_STREQ("foo", ss.GetNextToken());
    EXPECT_STREQ("bar", ss.GetNextToken());
    EXPECT_STREQ("baz", ss.GetNextToken());
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = "";
    StringSplitter ss(buf, 0, ',');
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = "\0";
    StringSplitter ss(buf, 1, ',');
    EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = ",,foo,bar\0,baz";
    StringSplitter ss(buf, sizeof(buf), ',');
    EXPECT_STREQ("foo", ss.GetNextToken());
    EXPECT_STREQ("bar", ss.GetNextToken());
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = ",,a\0,b,";
    StringSplitter ss(buf, sizeof(buf), ',');
    EXPECT_STREQ("a", ss.GetNextToken());
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = ",a,\0b";
    StringSplitter ss(buf, sizeof(buf), ',');
    EXPECT_STREQ("a", ss.GetNextToken());
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = ",a\0\0,x\0\0b";
    StringSplitter ss(buf, sizeof(buf), ',');
    EXPECT_STREQ("a", ss.GetNextToken());
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
}

TEST(StringSplitterTest, SplitOnNUL) {
  EXPECT_EQ(nullptr, StringSplitter(std::string(""), '\0').GetNextToken());
  {
    std::string str;
    str.resize(48);
    memcpy(&str[0], "foo\0", 4);
    memcpy(&str[4], "bar\0", 4);
    memcpy(&str[20], "baz", 3);
    StringSplitter ss(std::move(str), '\0');
    EXPECT_STREQ("foo", ss.GetNextToken());
    EXPECT_STREQ("bar", ss.GetNextToken());
    EXPECT_STREQ("baz", ss.GetNextToken());
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = "foo\0bar\0baz\0";
    StringSplitter ss(buf, sizeof(buf), '\0');
    EXPECT_STREQ("foo", ss.GetNextToken());
    EXPECT_STREQ("bar", ss.GetNextToken());
    EXPECT_STREQ("baz", ss.GetNextToken());
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = "\0\0foo\0\0\0\0bar\0baz\0\0";
    StringSplitter ss(buf, sizeof(buf), '\0');
    EXPECT_STREQ("foo", ss.GetNextToken());
    EXPECT_STREQ("bar", ss.GetNextToken());
    EXPECT_STREQ("baz", ss.GetNextToken());
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = "";
    StringSplitter ss(buf, 0, '\0');
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = "\0";
    StringSplitter ss(buf, 1, '\0');
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
  {
    char buf[] = "\0\0";
    StringSplitter ss(buf, 2, '\0');
    for (int i = 0; i < 3; i++)
      EXPECT_EQ(nullptr, ss.GetNextToken());
  }
}

}  // namespace
}  // namespace base
}  // namespace perfetto
