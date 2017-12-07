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

#ifndef INCLUDE_PERFETTO_TRACING_CORE_DATA_SOURCE_DESCRIPTOR_H_
#define INCLUDE_PERFETTO_TRACING_CORE_DATA_SOURCE_DESCRIPTOR_H_

#include <stdint.h>

#include <string>

#include "perfetto/tracing/core/proto_pimpl_macros.h"

namespace perfetto {

namespace protos {
class DataSourceDescriptor;  // From data_source_descriptor.proto .
}  // namespace protos

// See src/ipc/data_source_descriptor.proto for comments.
class DataSourceDescriptor {
 public:
  DataSourceDescriptor();
  explicit DataSourceDescriptor(protos::DataSourceDescriptor*);
  DataSourceDescriptor(DataSourceDescriptor&&) noexcept;
  ~DataSourceDescriptor();

  void set_name(const std::string&);
  const std::string& name() const;

  void CopyFrom(const protos::DataSourceDescriptor&);
  const protos::DataSourceDescriptor& proto() const { return *impl_; }

  PERFETTO_DECLARE_PROTO_PIMPL(DataSourceDescriptor,
                               protos::DataSourceDescriptor)
};

}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACING_CORE_DATA_SOURCE_DESCRIPTOR_H_
