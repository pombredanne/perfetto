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

#ifndef TRACING_INCLUDE_TRACING_TRACE_CONFIG_H_
#define TRACING_INCLUDE_TRACING_TRACE_CONFIG_H_

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <string>

#include "tracing/core/data_source_config.h"

namespace perfetto {

struct TraceConfig {
  struct BufferConfig {
    uint32_t size_kb = 0;
  };
  struct DataSource {
    std::list<std::string> producer_name_filter;
    DataSourceConfig config;
  };

  std::list<BufferConfig> buffers;
  std::list<DataSource> data_sources;
};

}  // namespace perfetto

#endif  // TRACING_INCLUDE_TRACING_TRACE_CONFIG_H_
