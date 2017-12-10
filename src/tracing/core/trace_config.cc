
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
 * protos/tracing_service/trace_config.proto
 * by
 * ../../tools/proto_to_cpp/proto_to_cpp.cc.
 * If you need to make changes here, change the .proto file and then run
 * ./tools/gen_tracing_cpp_headers_from_protos.py
 */

#include "include/perfetto/tracing/core/trace_config.h"

#include "protos/tracing_service/data_source_config.pb.h"
#include "protos/tracing_service/trace_config.pb.h"

namespace perfetto {

TraceConfig::TraceConfig() = default;
TraceConfig::~TraceConfig() = default;
TraceConfig::TraceConfig(TraceConfig&&) noexcept = default;
TraceConfig& TraceConfig::operator=(TraceConfig&&) = default;

void TraceConfig::FromProto(const perfetto::protos::TraceConfig& proto) {
  buffers_.clear();
  for (const auto& field : proto.buffers()) {
    buffers_.emplace_back();
    buffers_.back().FromProto(field);
  }
  data_sources_.clear();
  for (const auto& field : proto.data_sources()) {
    data_sources_.emplace_back();
    data_sources_.back().FromProto(field);
  }
  duration_ms_ = static_cast<decltype(duration_ms_)>(proto.duration_ms());
  unknown_fields_ = proto.unknown_fields();
}

void TraceConfig::ToProto(perfetto::protos::TraceConfig* proto) const {
  proto->Clear();
  for (const auto& it : buffers_) {
    auto* entry = proto->add_buffers();
    it.ToProto(entry);
  }
  for (const auto& it : data_sources_) {
    auto* entry = proto->add_data_sources();
    it.ToProto(entry);
  }
  proto->set_duration_ms(
      static_cast<decltype(proto->duration_ms())>(duration_ms_));
  *(proto->mutable_unknown_fields()) = unknown_fields_;
}

TraceConfig::BufferConfig::BufferConfig() = default;
TraceConfig::BufferConfig::~BufferConfig() = default;
TraceConfig::BufferConfig::BufferConfig(TraceConfig::BufferConfig&&) noexcept =
    default;
TraceConfig::BufferConfig& TraceConfig::BufferConfig::operator=(
    TraceConfig::BufferConfig&&) = default;

void TraceConfig::BufferConfig::FromProto(
    const perfetto::protos::TraceConfig_BufferConfig& proto) {
  size_kb_ = static_cast<decltype(size_kb_)>(proto.size_kb());
  optimize_for_ = static_cast<decltype(optimize_for_)>(proto.optimize_for());
  fill_policy_ = static_cast<decltype(fill_policy_)>(proto.fill_policy());
  unknown_fields_ = proto.unknown_fields();
}

void TraceConfig::BufferConfig::ToProto(
    perfetto::protos::TraceConfig_BufferConfig* proto) const {
  proto->Clear();
  proto->set_size_kb(static_cast<decltype(proto->size_kb())>(size_kb_));
  proto->set_optimize_for(
      static_cast<decltype(proto->optimize_for())>(optimize_for_));
  proto->set_fill_policy(
      static_cast<decltype(proto->fill_policy())>(fill_policy_));
  *(proto->mutable_unknown_fields()) = unknown_fields_;
}

TraceConfig::DataSource::DataSource() = default;
TraceConfig::DataSource::~DataSource() = default;
TraceConfig::DataSource::DataSource(TraceConfig::DataSource&&) noexcept =
    default;
TraceConfig::DataSource& TraceConfig::DataSource::operator=(
    TraceConfig::DataSource&&) = default;

void TraceConfig::DataSource::FromProto(
    const perfetto::protos::TraceConfig_DataSource& proto) {
  config_.FromProto(proto.config());
  producer_name_filter_.clear();
  for (const auto& field : proto.producer_name_filter()) {
    producer_name_filter_.emplace_back();
    producer_name_filter_.back() =
        static_cast<decltype(producer_name_filter_)::value_type>(field);
  }
  unknown_fields_ = proto.unknown_fields();
}

void TraceConfig::DataSource::ToProto(
    perfetto::protos::TraceConfig_DataSource* proto) const {
  proto->Clear();
  config_.ToProto(proto->mutable_config());
  for (const auto& it : producer_name_filter_) {
    auto* entry = proto->add_producer_name_filter();
    *entry = static_cast<decltype(proto->producer_name_filter(0))>(it);
  }
  *(proto->mutable_unknown_fields()) = unknown_fields_;
}

}  // namespace perfetto
