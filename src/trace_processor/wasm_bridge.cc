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

#include "perfetto/trace_processor/trace_processor.h"

#include <emscripten/emscripten.h>
#include <map>
#include <string>

#include "perfetto/processed_trace/query.pb.h"
#include "perfetto/trace_processor/sched.h"
#include "src/trace_processor/emscripten_task_runner.h"

namespace perfetto {
namespace trace_processor {

using TraceID = int;

// Imported functions implemented by JS and injected via Initialize().

// ReadTrace(): reads a portion of the trace file.
// Args:
//   trace_id: ID of a trace previously obtained through a call to
//             CreateTraceProcessor(). Identifies the trace for which the C++
//             code is requesting the data from.
//   offset: the start offset (in bytes) in the trace file to read.
//   len: the size of the buffered returned.
// Returns:
//   The number of bytes read, which must be <= |len|.
// TODO(primiano): where does it read into? We need a JS<>C++ mem buffer.
using ReadTraceFunction = uint32_t (*)(TraceID,
                                       uint32_t /*offset*/,
                                       uint32_t /*len*/);
namespace {

EmscriptenTaskRunner* g_task_runner;
TraceID g_trace_id;
ReadTraceFunction g_read_trace;
std::unique_ptr<TraceProcessor> g_trace_processor;

}  // namespace

extern "C" {
void EMSCRIPTEN_KEEPALIVE Initialize(TraceID, ReadTraceFunction);

// Functions exported to JS.
void EMSCRIPTEN_KEEPALIVE GetSlices(const char* query_data, int len);

void GetSlices(const char* query_data, int len) {
  protos::Query query;
  bool parsed = query.ParseFromArray(query_data, len);
  printf("GetSlices(%p, %d). Parsed = %d\n",
         reinterpret_cast<const void*>(query_data), len, parsed);
}

void Initialize(TraceID trace_id, ReadTraceFunction read_trace_function) {
  printf("In Initialize()\n");
  g_trace_id = trace_id;
  g_task_runner = new EmscriptenTaskRunner();
  g_read_trace = read_trace_function;
  g_trace_processor.reset(new TraceProcessor(nullptr, nullptr));
}
}

}  // namespace trace_processor

}  // namespace perfetto
}  // namespace perfetto
