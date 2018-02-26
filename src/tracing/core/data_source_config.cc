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

/*******************************************************************************
 * AUTOGENERATED - DO NOT EDIT
 *******************************************************************************
 * This file has been generated from the protobuf message
 * perfetto/config/data_source_config.proto
 * by
 * ../../tools/proto_to_cpp/proto_to_cpp.cc.
 * If you need to make changes here, change the .proto file and then run
 * ./tools/gen_tracing_cpp_headers_from_protos.py
 */

#include "perfetto/tracing/core/data_source_config.h"

#include "perfetto/config/data_source_config.pb.h"
#include "perfetto/config/ftrace/ftrace_config.pb.h"

namespace perfetto {

DataSourceConfig::DataSourceConfig() = default;
DataSourceConfig::~DataSourceConfig() = default;
DataSourceConfig::DataSourceConfig(const DataSourceConfig&) = default;
DataSourceConfig& DataSourceConfig::operator=(const DataSourceConfig&) =
    default;
DataSourceConfig::DataSourceConfig(DataSourceConfig&&) noexcept = default;
DataSourceConfig& DataSourceConfig::operator=(DataSourceConfig&&) = default;

void DataSourceConfig::FromProto(
    const perfetto::protos::DataSourceConfig& proto) {
  static_assert(sizeof(name_) == sizeof(proto.name()), "size mismatch");
  name_ = static_cast<decltype(name_)>(proto.name());

  static_assert(sizeof(target_buffer_) == sizeof(proto.target_buffer()),
                "size mismatch");
  target_buffer_ = static_cast<decltype(target_buffer_)>(proto.target_buffer());

  static_assert(sizeof(trace_duration_ms_) == sizeof(proto.trace_duration_ms()),
                "size mismatch");
  trace_duration_ms_ =
      static_cast<decltype(trace_duration_ms_)>(proto.trace_duration_ms());

  ftrace_config_.FromProto(proto.ftrace_config());
  unknown_fields_ = proto.unknown_fields();
}

void DataSourceConfig::ToProto(
    perfetto::protos::DataSourceConfig* proto) const {
  proto->Clear();

  static_assert(sizeof(name_) == sizeof(proto->name()), "size mismatch");
  proto->set_name(static_cast<decltype(proto->name())>(name_));

  static_assert(sizeof(target_buffer_) == sizeof(proto->target_buffer()),
                "size mismatch");
  proto->set_target_buffer(
      static_cast<decltype(proto->target_buffer())>(target_buffer_));

  static_assert(
      sizeof(trace_duration_ms_) == sizeof(proto->trace_duration_ms()),
      "size mismatch");
  proto->set_trace_duration_ms(
      static_cast<decltype(proto->trace_duration_ms())>(trace_duration_ms_));

  ftrace_config_.ToProto(proto->mutable_ftrace_config());
  *(proto->mutable_unknown_fields()) = unknown_fields_;
}

}  // namespace perfetto
