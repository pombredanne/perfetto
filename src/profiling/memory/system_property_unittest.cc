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

#include "src/profiling/memory/system_property.h"

#include "gtest/gtest.h"

#include <android-base/properties.h>

namespace perfetto {
namespace profiling {
namespace {

TEST(SystemPropertyTest, All) {
  SystemProperties prop;
  auto handle = prop.SetAll();
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "all");
}

TEST(SystemPropertyTest, CleanupAll) {
  SystemProperties prop;
  {
    auto handle = prop.SetAll();
    ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "all");
  }
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "");
}

TEST(SystemPropertyTest, Specific) {
  SystemProperties prop;
  auto handle2 = prop.SetProperty("system_server");
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "1");
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable.system_server", ""),
            "1");
}

TEST(SystemPropertyTest, CleanupSpecific) {
  SystemProperties prop;
  {
    auto handle2 = prop.SetProperty("system_server");
    ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "1");
    ASSERT_EQ(android::base::GetProperty("heapprofd.enable.system_server", ""),
              "1");
  }
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "");
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable.system_server", ""),
            "");
}

TEST(SystemPropertyTest, AllAndSpecific) {
  SystemProperties prop;
  auto handle = prop.SetAll();
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "all");
  auto handle2 = prop.SetProperty("system_server");
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "all");
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable.system_server", ""),
            "1");
  { SystemProperties::Handle destroy = std::move(handle); }
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable", ""), "1");
  ASSERT_EQ(android::base::GetProperty("heapprofd.enable.system_server", ""),
            "1");
}

}  // namespace
}  // namespace profiling
}  // namespace perfetto
