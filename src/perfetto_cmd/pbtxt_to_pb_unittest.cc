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

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "perfetto/config/trace_config.pb.h"

namespace perfetto {
namespace {

using ::google::protobuf::io::ZeroCopyInputStream;
using ::google::protobuf::io::ArrayInputStream;

TEST(PbtxtToPb, Example) {
  std::string text = "duration_ms: 1234";
  ArrayInputStream input(text.c_str(), static_cast<int>(text.size()));

  std::vector<uint8_t> output = PbtxtToPb(&input);

  protos::TraceConfig config;
  config.ParseFromArray(output.data(), static_cast<int>(output.size()));
  EXPECT_EQ(config.duration_ms(), 1234);
}

}  // namespace
}  // namespace perfetto
