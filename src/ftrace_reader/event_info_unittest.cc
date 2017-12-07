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

#include "event_info.h"

#include "gtest/gtest.h"

namespace perfetto {
namespace {

TEST(GetStaticEventInfo, SanityCheck) {
  std::vector<Event> events = GetStaticEventInfo();
  for (const Event& event : events) {
    // For each event the following fields should be filled
    // statically:
    // Non-empty name.
    ASSERT_FALSE(event.name.empty());
    // Non-empty group.
    ASSERT_FALSE(event.group.empty());
    // Non-zero proto field id.
    ASSERT_TRUE(event.proto_field_id);

    for (const Field& field : event.fields) {
      // Non-empty name.
      ASSERT_FALSE(field.ftrace_name.empty());
      // Non-zero proto field id.
      ASSERT_TRUE(field.proto_field_id);
      // Should have set the proto field type.
      ASSERT_TRUE(field.proto_field_type);
    }
  }
}

}  // namespace
}  // namespace perfetto
