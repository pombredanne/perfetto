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

#include "src/perfetto_cmd/pbtxt_to_pb.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "perfetto/config/trace_config.pb.h"

namespace perfetto {
namespace {

using ::testing::StrictMock;
using ::testing::ElementsAre;
using ::google::protobuf::io::ZeroCopyInputStream;
using ::google::protobuf::io::ArrayInputStream;

class MockErrorReporter : public ErrorReporter {
 public:
  MockErrorReporter() {}
  ~MockErrorReporter() = default;
  MOCK_METHOD4(AddError,
               void(size_t line,
                    size_t column_start,
                    size_t column_end,
                    const std::string& message));
};

protos::TraceConfig ToProto(const std::string& input) {
  StrictMock<MockErrorReporter> reporter;
  std::vector<uint8_t> output = PbtxtToPb(input, &reporter);
  protos::TraceConfig config;
  config.ParseFromArray(output.data(), static_cast<int>(output.size()));
  return config;
}

void ToErrors(const std::string& input, MockErrorReporter* reporter) {
  PbtxtToPb(input, reporter);
}

TEST(PbtxtToPb, OneField) {
  protos::TraceConfig config = ToProto(R"(
    duration_ms: 1234
  )");
  EXPECT_EQ(config.duration_ms(), 1234);
}

TEST(PbtxtToPb, TwoFields) {
  protos::TraceConfig config = ToProto(R"(
    duration_ms: 1234
    file_write_period_ms: 5678
  )");
  EXPECT_EQ(config.duration_ms(), 1234);
  EXPECT_EQ(config.file_write_period_ms(), 5678);
}

TEST(PbtxtToPb, Semicolons) {
  protos::TraceConfig config = ToProto(R"(
    duration_ms: 1234;
    file_write_period_ms: 5678;
  )");
  EXPECT_EQ(config.duration_ms(), 1234);
  EXPECT_EQ(config.file_write_period_ms(), 5678);
}

TEST(PbtxtToPb, NestedMessage) {
  protos::TraceConfig config = ToProto(R"(
    buffers: {
      size_kb: 123
    }
  )");
  ASSERT_EQ(config.buffers().size(), 1);
  EXPECT_EQ(config.buffers().Get(0).size_kb(), 123);
}

TEST(PbtxtToPb, MultipleNestedMessage) {
  protos::TraceConfig config = ToProto(R"(
    buffers: {
      size_kb: 1
    }
    buffers: {
      size_kb: 2
    }
  )");
  ASSERT_EQ(config.buffers().size(), 2);
  EXPECT_EQ(config.buffers().Get(0).size_kb(), 1);
  EXPECT_EQ(config.buffers().Get(1).size_kb(), 2);
}

TEST(PbtxtToPb, Booleans) {
  protos::TraceConfig config = ToProto(R"(
    write_into_file: false; deferred_start: true;
  )");
  EXPECT_EQ(config.write_into_file(), false);
  EXPECT_EQ(config.deferred_start(), true);
}

TEST(PbtxtToPb, UnknownField) {
  MockErrorReporter reporter;
  EXPECT_CALL(
      reporter,
      AddError(0, 0, 0,
               "No field with name \"not_a_label\" in proto TraceConfig."));
  ToErrors(R"(
    not_a_label: false
  )",
           &reporter);
}

TEST(PbtxtToPb, BadBoolean) {
  MockErrorReporter reporter;
  EXPECT_CALL(reporter,
              AddError(0, 0, 0, "Expected 'true' or 'false' instead saw: foo"));
  ToErrors(R"(
    write_into_file: foo;
  )",
           &reporter);
}

TEST(PbtxtToPb, MissingBoolean) {
  MockErrorReporter reporter;
  EXPECT_CALL(reporter,
              AddError(0, 0, 0, "Expected 'true' or 'false' instead saw: "));
  ToErrors(R"(
    write_into_file:
  )",
           &reporter);
}

// TEST(PbtxtToPb, RootProtoMustNotEndWithBrace) {
//  MockErrorReporter reporter;
//  EXPECT_CALL(reporter, AddError(0,0,0, "Expected 'true' or 'false' instead
//  saw: ")); ToErrors(R"(
//    }
//  )", &reporter);
//}

}  // namespace
}  // namespace perfetto
