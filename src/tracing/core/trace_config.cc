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

#include "tracing/core/trace_config.h"

#include "protos/tracing_service/data_source_config.pb.h"
#include "protos/tracing_service/trace_config.pb.h"

namespace perfetto {

// Root TraceConfig class.
PERFETTO_DEFINE_CTOR_AND_COPY_OPERATORS(TraceConfig,
                                        TraceConfig,
                                        protos::TraceConfig)
PERFETTO_DEFINE_REPEATED_SUBTYPE_ACCESSORS(TraceConfig,
                                           TraceConfig::BufferConfig,
                                           buffers)
PERFETTO_DEFINE_REPEATED_SUBTYPE_ACCESSORS(TraceConfig,
                                           TraceConfig::DataSource,
                                           data_sources)

// TraceConfig::BufferConfig inner class.
PERFETTO_DEFINE_CTOR_AND_COPY_OPERATORS(TraceConfig::BufferConfig,
                                        BufferConfig,
                                        protos::TraceConfig_BufferConfig)
PERFETTO_DEFINE_POD_ACCESSORS(TraceConfig::BufferConfig, uint32_t, size_kb)
PERFETTO_DEFINE_ENUM_ACCESSORS(TraceConfig::BufferConfig,
                               TraceConfig::BufferConfig::OptimizeFor,
                               protos::TraceConfig_BufferConfig_OptimizeFor,
                               optimize_for)
PERFETTO_DEFINE_ENUM_ACCESSORS(TraceConfig::BufferConfig,
                               TraceConfig::BufferConfig::FillPolicy,
                               protos::TraceConfig_BufferConfig_FillPolicy,
                               fill_policy)

// TraceConfig::DataSource inner class.
PERFETTO_DEFINE_CTOR_AND_COPY_OPERATORS(TraceConfig::DataSource,
                                        DataSource,
                                        protos::TraceConfig_DataSource)
PERFETTO_DEFINE_SUBTYPE_ACCESSORS(TraceConfig::DataSource,
                                  DataSourceConfig,
                                  config)
PERFETTO_DEFINE_REPEATED_ACCESSORS(TraceConfig::DataSource,
                                   std::string,
                                   producer_name_filter)

}  // namespace perfetto
