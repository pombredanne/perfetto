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

#ifndef SRC_FTRACE_READER_EVENT_PROTO_INFO_H_
#define SRC_FTRACE_READER_EVENT_PROTO_INFO_H_

#include <stdint.h>

#include <string>
#include <vector>

enum ProtoFieldType {
  kProtoNumber = 1,
  kProtoString,
  kProtoInt32,
};

enum FtraceFieldType {
  kFtraceNumber = 1,
};

struct Field {
  size_t ftrace_offset;
  size_t ftrace_size;
  FtraceFieldType ftrace_type;
  size_t proto_field_id;
  ProtoFieldType proto_field_type;
  std::string ftrace_name;
};

struct Event {
  std::string name;
  std::string group;
  std::vector<Field> fields;
  size_t ftrace_event_id;
  size_t proto_field_id;
};

std::vector<Event> GetStaticEventInfo();

#endif  // SRC_FTRACE_READER_EVENT_PROTO_INFO_H_
