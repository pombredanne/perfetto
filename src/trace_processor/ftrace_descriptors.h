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

#ifndef SRC_TRACE_PROCESSOR_FTRACE_DESCRIPTORS_H_
#define SRC_TRACE_PROCESSOR_FTRACE_DESCRIPTORS_H_

#include <array>
#include "perfetto/protozero/proto_utils.h"

namespace perfetto {

using protozero::proto_utils::ProtoFieldType;

struct FieldDescriptor {
  const char* name;
  ProtoFieldType type;
};

struct MessageDescriptor {
  const char* name;
  FieldDescriptor fields[32];
};

MessageDescriptor* GetMessageDescriptorForId(size_t id);
size_t DescriptorsSize();

}  // namespace perfetto
#endif  // SRC_TRACE_PROCESSOR_FTRACE_DESCRIPTORS_H_
