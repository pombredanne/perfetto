

/*
 * Copyright (C) 2019 The Android Open Source Project
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

// #include <stddef.h>
// #include <stdint.h>
//
// #include <algorithm>
//
// #include "perfetto/base/logging.h"
// #include "perfetto/base/utils.h"
// #include "perfetto/protozero/scattered_stream_null_delegate.h"
// #include "perfetto/protozero/scattered_stream_writer.h"
// #include "perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
// #include "src/traced/probes/ftrace/cpu_reader.h"
// #include "test/cpu_reader_support.h"

#include <map>
#include <random>
#include <string>

#include "gtest/gtest.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/base/test/utils.h"
#include "src/trace_processor/json_trace_parser.h"

#include "perfetto/trace_processor/raw_query.pb.h"

namespace perfetto {
namespace trace_processor {

void FuzzTraceProcessor(const uint8_t* data, size_t size);

void FuzzTraceProcessor(const uint8_t* data, size_t size) {
  std::unique_ptr<TraceProcessor> processor =
      TraceProcessor::CreateInstance(Config());
  std::unique_ptr<uint8_t[]> buf(new uint8_t[size]);
  memcpy(buf.get(), data, size);
  if (!processor->Parse(std::move(buf), size))
    return;
  processor->NotifyEndOfFile();
}

}  // namespace trace_processor
}  // namespace perfetto

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  perfetto::trace_processor::FuzzTraceProcessor(data, size);
  return 0;
}
