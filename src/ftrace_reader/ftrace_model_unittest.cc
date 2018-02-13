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

#include "ftrace_model.h"

#include <memory>

#include "ftrace_procfs.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "proto_translation_table.h"

using testing::AnyNumber;
using testing::IsEmpty;
using testing::Return;
using testing::UnorderedElementsAre;

namespace perfetto {
namespace {

class MockFtraceProcfs : public FtraceProcfs {
 public:
  MockFtraceProcfs() : FtraceProcfs("/root/") {
    ON_CALL(*this, NumberOfCpus()).WillByDefault(Return(1));
    EXPECT_CALL(*this, NumberOfCpus()).Times(AnyNumber());
  }

  MOCK_METHOD2(WriteToFile,
               bool(const std::string& path, const std::string& str));
  MOCK_METHOD1(ReadOneCharFromFile, char(const std::string& path));
  MOCK_METHOD1(ClearFile, bool(const std::string& path));
  MOCK_CONST_METHOD1(ReadFileIntoString, std::string(const std::string& path));
  MOCK_CONST_METHOD0(NumberOfCpus, size_t());
};

std::unique_ptr<ProtoTranslationTable> CreateFakeTable() {
  std::vector<Field> common_fields;
  std::vector<Event> events;

  {
    Event event;
    event.name = "sched_switch";
    event.group = "sched";
    event.ftrace_event_id = 1;
    events.push_back(event);
  }

  {
    Event event;
    event.name = "sched_wakeup";
    event.group = "sched";
    event.ftrace_event_id = 10;
    events.push_back(event);
  }

  {
    Event event;
    event.name = "sched_new";
    event.group = "sched";
    event.ftrace_event_id = 20;
    events.push_back(event);
  }

  return std::unique_ptr<ProtoTranslationTable>(
      new ProtoTranslationTable(events, std::move(common_fields)));
}

TEST(ComputeFtraceStateTest, NoConfigs) {
  std::unique_ptr<ProtoTranslationTable> table = CreateFakeTable();
  FtraceState state = ComputeFtraceState({});
  EXPECT_FALSE(state.ftrace_on());
  EXPECT_EQ(state.cpu_buffer_size_pages(), 0u);
  EXPECT_THAT(state.ftrace_events(), IsEmpty());
}

TEST(ComputeFtraceStateTest, EmptyConfig) {
  std::unique_ptr<ProtoTranslationTable> table = CreateFakeTable();
  FtraceConfig config = CreateFtraceConfig({});
  FtraceState state = ComputeFtraceState({&config});
  EXPECT_TRUE(state.ftrace_on());
  // No buffer size given: good default.
  EXPECT_EQ(state.cpu_buffer_size_pages(), 128u);
  EXPECT_THAT(state.ftrace_events(), IsEmpty());
  EXPECT_THAT(state.atrace_categories(), IsEmpty());
  EXPECT_THAT(state.atrace_apps(), IsEmpty());
}

TEST(ComputeFtraceStateTest, OneConfig) {
  std::unique_ptr<ProtoTranslationTable> table = CreateFakeTable();
  FtraceConfig config = CreateFtraceConfig({"sched_switch"});
  config.set_buffer_size_kb(42);
  *config.add_atrace_categories() = "sched";
  *config.add_atrace_apps() = "com.google.blah";
  FtraceState state = ComputeFtraceState({&config});

  EXPECT_TRUE(state.ftrace_on());
  EXPECT_EQ(state.cpu_buffer_size_pages(), 10u);
  EXPECT_THAT(state.ftrace_events(), UnorderedElementsAre("sched_switch"));
  EXPECT_THAT(state.atrace_categories(), UnorderedElementsAre("sched"));
  EXPECT_THAT(state.atrace_apps(), UnorderedElementsAre("com.google.blah"));
}

TEST(ComputeFtraceStateTest, MultipleConfigs) {
  std::unique_ptr<ProtoTranslationTable> table = CreateFakeTable();
  FtraceConfig config_a = CreateFtraceConfig({"sched_switch", "sched_new"});
  *config_a.add_atrace_categories() = "sched";

  FtraceConfig config_b = CreateFtraceConfig({"sched_switch", "sched_wakeup"});
  *config_b.add_atrace_apps() = "com.google.blah";

  FtraceState state = ComputeFtraceState({&config_a, &config_b});

  EXPECT_TRUE(state.ftrace_on());
  EXPECT_THAT(
      state.ftrace_events(),
      UnorderedElementsAre("sched_switch", "sched_wakeup", "sched_new"));
  EXPECT_THAT(state.atrace_categories(), UnorderedElementsAre("sched"));
  EXPECT_THAT(state.atrace_apps(), UnorderedElementsAre("com.google.blah"));
}

TEST(ComputeFtraceStateTest, BufferSizes) {
  std::unique_ptr<ProtoTranslationTable> table = CreateFakeTable();

  {
    // No buffer size given: good default (128 pages = 512kb).
    FtraceConfig config;
    FtraceState state = ComputeFtraceState({&config});
    EXPECT_EQ(state.cpu_buffer_size_pages(), 128u);
  }

  {
    // Buffer size given way too big: good default.
    FtraceConfig config;
    config.set_buffer_size_kb(10 * 1024 * 1024);
    FtraceState state = ComputeFtraceState({&config});
    EXPECT_EQ(state.cpu_buffer_size_pages(), 128u);
  }

  {
    // The limit is 2mb per CPU, 3mb is too much.
    FtraceConfig config;
    config.set_buffer_size_kb(3 * 1024);
    FtraceState state = ComputeFtraceState({&config});
    EXPECT_EQ(state.cpu_buffer_size_pages(), 128u);
  }

  {
    // Your size ends up with less than 1 page per cpu -> 1 page.
    FtraceConfig config;
    config.set_buffer_size_kb(1);
    FtraceState state = ComputeFtraceState({&config});
    EXPECT_EQ(state.cpu_buffer_size_pages(), 1u);
  }

  {
    // You picked a good size -> your size rounded to nearest page.
    FtraceConfig config;
    config.set_buffer_size_kb(42);
    FtraceState state = ComputeFtraceState({&config});
    EXPECT_EQ(state.cpu_buffer_size_pages(), 10u);
  }

  {
    // Multiple configs: take the max then as above.
    FtraceConfig config_a;
    config_a.set_buffer_size_kb(0);
    FtraceConfig config_b;
    config_b.set_buffer_size_kb(42);
    FtraceState state = ComputeFtraceState({&config_a, &config_b});
    EXPECT_EQ(state.cpu_buffer_size_pages(), 10u);
  }

  {
    // Multiple configs: take the max then as above.
    FtraceConfig config_a;
    config_a.set_buffer_size_kb(10 * 1024 * 1024);
    FtraceConfig config_b;
    config_b.set_buffer_size_kb(42);
    FtraceState state = ComputeFtraceState({&config_a, &config_b});
    EXPECT_EQ(state.cpu_buffer_size_pages(), 128u);
  }
}

TEST(FtraceModelTest, TurnFtraceOnOff) {
  std::unique_ptr<ProtoTranslationTable> table = CreateFakeTable();
  MockFtraceProcfs ftrace;

  FtraceConfig config = CreateFtraceConfig({"sched_switch"});

  FtraceModel model(&ftrace, table.get());

  ON_CALL(ftrace, ReadFileIntoString("/root/trace_clock"))
      .WillByDefault(Return("[local] global boot"));
  EXPECT_CALL(ftrace, ReadFileIntoString("/root/trace_clock"))
      .Times(AnyNumber());

  EXPECT_CALL(ftrace, ReadOneCharFromFile("/root/tracing_on"))
      .WillOnce(Return('0'));
  EXPECT_CALL(ftrace, WriteToFile("/root/buffer_size_kb", "512"));
  EXPECT_CALL(ftrace, WriteToFile("/root/trace_clock", "boot"));
  EXPECT_CALL(ftrace, WriteToFile("/root/tracing_on", "1"));
  EXPECT_CALL(ftrace,
              WriteToFile("/root/events/sched/sched_switch/enable", "1"))
      .WillOnce(Return(true));
  ASSERT_TRUE(model.AddConfig(&config));

  EXPECT_CALL(ftrace, ReadOneCharFromFile("/root/tracing_on"))
      .WillOnce(Return('1'));
  EXPECT_CALL(ftrace, WriteToFile("/root/tracing_on", "0"));
  EXPECT_CALL(ftrace, WriteToFile("/root/buffer_size_kb", "0"));
  EXPECT_CALL(ftrace, WriteToFile("/root/events/enable", "0"));
  EXPECT_CALL(ftrace,
              WriteToFile("/root/events/sched/sched_switch/enable", "0"))
      .WillOnce(Return(true));
  EXPECT_CALL(ftrace, ClearFile("/root/trace")).WillOnce(Return(true));
  ASSERT_TRUE(model.RemoveConfig(&config));
}

TEST(FtraceModelTest, FtraceIsAlreadyOn) {
  std::unique_ptr<ProtoTranslationTable> table = CreateFakeTable();
  MockFtraceProcfs ftrace;

  FtraceConfig config = CreateFtraceConfig({"sched_switch"});

  FtraceModel model(&ftrace, table.get());

  // If someone is using ftrace already don't stomp on what they are doing.
  EXPECT_CALL(ftrace, ReadOneCharFromFile("/root/tracing_on"))
      .WillOnce(Return('1'));
  ASSERT_FALSE(model.AddConfig(&config));
}

TEST(FtraceModelTest, SetupClockForTesting) {
  std::unique_ptr<ProtoTranslationTable> table = CreateFakeTable();
  MockFtraceProcfs ftrace;

  FtraceModel model(&ftrace, table.get());

  EXPECT_CALL(ftrace, ReadFileIntoString("/root/trace_clock"))
      .Times(AnyNumber());

  ON_CALL(ftrace, ReadFileIntoString("/root/trace_clock"))
      .WillByDefault(Return("[local] global boot"));
  EXPECT_CALL(ftrace, WriteToFile("/root/trace_clock", "boot"));
  model.SetupClockForTesting();

  ON_CALL(ftrace, ReadFileIntoString("/root/trace_clock"))
      .WillByDefault(Return("[local] global"));
  EXPECT_CALL(ftrace, WriteToFile("/root/trace_clock", "global"));
  model.SetupClockForTesting();

  ON_CALL(ftrace, ReadFileIntoString("/root/trace_clock"))
      .WillByDefault(Return(""));
  model.SetupClockForTesting();

  ON_CALL(ftrace, ReadFileIntoString("/root/trace_clock"))
      .WillByDefault(Return("local [global]"));
  model.SetupClockForTesting();
}

}  // namespace
}  // namespace perfetto
