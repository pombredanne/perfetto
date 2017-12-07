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

#ifndef INCLUDE_PERFETTO_TRACING_CORE_DATA_SOURCE_CONFIG_H_
#define INCLUDE_PERFETTO_TRACING_CORE_DATA_SOURCE_CONFIG_H_

#include <stdint.h>

#include <string>

#include "perfetto/tracing/core/proto_pimpl_macros.h"

namespace perfetto {

namespace protos {
class DataSourceConfig;  // From data_source_config.proto .
}  // namespace protos

// This class contains the configuration that the Service sends back to the
// Producer when it tells it to enable a given data source. This is the way
// that, for instance, the Service will tell the producer "turn tracing on,
// enable categories 'foo' and 'bar' and emit only the fields X and Y".

// This has to be kept in sync with src/ipc/data_source_config.proto via a pimpl
// pattern (http://en.cppreference.com/w/cpp/language/pimpl).
class DataSourceConfig {
 public:
  DataSourceConfig();
  explicit DataSourceConfig(protos::DataSourceConfig*);
  DataSourceConfig(DataSourceConfig&&) noexcept;
  ~DataSourceConfig();

  void set_name(const std::string&);
  const std::string& name() const;

  uint32_t target_buffer() const;
  void set_target_buffer(uint32_t);

  // TODO(primiano): temporary, for testing only.
  void set_trace_category_filters(const std::string&);
  const std::string& trace_category_filters() const;

  void CopyFrom(const protos::DataSourceConfig&);
  const protos::DataSourceConfig& proto() const { return *impl_; }

  PERFETTO_DECLARE_PROTO_PIMPL(DataSourceConfig, protos::DataSourceConfig)
};

}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACING_CORE_DATA_SOURCE_CONFIG_H_
