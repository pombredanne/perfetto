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

#include "ftrace_to_proto_translation_table.h"

#include "gtest/gtest.h"

using testing::ValuesIn;
using testing::TestWithParam;

namespace perfetto {
namespace {

class AllTranslationTableTest : public TestWithParam<const char*> {
 public:
  void SetUp() override {
    std::string path =
        "ftrace_reader/test/data/" + std::string(GetParam()) + "/";
    table_ = FtraceToProtoTranslationTable::Create(path);
  }

  std::unique_ptr<FtraceToProtoTranslationTable> table_;
};

const char* kDevices[] = {"android_seed_N2F62_3.10.49",
                          "android_hammerhead_MRA59G_3.4.0"};

TEST_P(AllTranslationTableTest, Create) {
  EXPECT_TRUE(table_);
}

INSTANTIATE_TEST_CASE_P(ByDevice, AllTranslationTableTest, ValuesIn(kDevices));

TEST(TranslationTable, Seed) {
  std::string path = "ftrace_reader/test/data/android_seed_N2F62_3.10.49/";
  auto table = FtraceToProtoTranslationTable::Create(path);

  EXPECT_EQ(table->largest_id(), 744);
  EXPECT_EQ(table->common_fields().at(0).ftrace_offset, 0u);
  EXPECT_EQ(table->common_fields().at(0).ftrace_size, 2u);

  auto sched_switch_event = table->GetEventByName("sched_switch");
  EXPECT_EQ(sched_switch_event->name, "sched_switch");
  EXPECT_EQ(sched_switch_event->group, "sched");
  EXPECT_EQ(sched_switch_event->ftrace_event_id, 68);
  EXPECT_EQ(sched_switch_event->fields.at(0).ftrace_offset, 8u);
  EXPECT_EQ(sched_switch_event->fields.at(0).ftrace_size, 16u);
}

TEST(TranslationTable, Getters) {
  using Event = FtraceToProtoTranslationTable::Event;
  using Field = FtraceToProtoTranslationTable::Field;

  std::vector<Field> common_fields;
  std::vector<Event> events;

  {
    Event event;
    event.name = "foo";
    event.ftrace_event_id = 1;
    events.push_back(event);
  }

  {
    Event event;
    event.name = "bar";
    event.ftrace_event_id = 2;
    events.push_back(event);
  }

  {
    Event event;
    event.name = "baz";
    event.ftrace_event_id = 100;
    events.push_back(event);
  }

  FtraceToProtoTranslationTable table(events, std::move(common_fields));
  EXPECT_EQ(table.largest_id(), 100);
  EXPECT_EQ(table.EventNameToFtraceId("foo"), 1);
  EXPECT_EQ(table.EventNameToFtraceId("baz"), 100);
  EXPECT_EQ(table.EventNameToFtraceId("no_such_event"), 0);
  EXPECT_EQ(table.GetEventById(1)->name, "foo");
  EXPECT_EQ(table.GetEventById(3), nullptr);
  EXPECT_EQ(table.GetEventById(200), nullptr);
  EXPECT_EQ(table.GetEventById(0), nullptr);
}

}  // namespace
}  // namespace perfetto
