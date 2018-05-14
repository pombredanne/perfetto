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

#include "perfetto/trace_processor/query.pb.h"
#include "perfetto/trace_processor/sched.pb.h"
#include "perfetto/trace_processor/blob_reader.h"
#include "perfetto/trace_processor/sched.h"
#include "src/trace_processor/emscripten_task_runner.h"

namespace perfetto {
namespace trace_processor {

using TraceID = uint32_t;
using RequestID = uint32_t;

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
using ReplyFunction = void (*)(RequestID,
                               bool success,
                               const char* /*proto_reply_data*/,
                               uint32_t /*len*/);

namespace {

// TODO(primiano): create a class to handle the module state, like civilization.
#pragma GCC diagnostic ignored "-Wglobal-constructors"
#pragma GCC diagnostic ignored "-Wexit-time-destructors"

EmscriptenTaskRunner* g_task_runner;
TraceID g_trace_id;
ReadTraceFunction g_read_trace;
ReplyFunction g_reply;
TraceProcessor* g_trace_processor;
BlobReader::ReadCallback g_read_callback;

class BlobReaderImpl : public BlobReader {
 public:
  explicit BlobReaderImpl(base::TaskRunner* task_runner)
      : task_runner_(task_runner) {}
  ~BlobReaderImpl() override = default;

  void Read(uint32_t offset, size_t max_size, ReadCallback callback) override {
    g_read_trace(g_trace_id, offset, max_size);
    g_read_callback = callback;
    (void)task_runner_;
  }

 private:
  base::TaskRunner* const task_runner_;
};

BlobReaderImpl* blob_reader() {
  static BlobReaderImpl* instance = new BlobReaderImpl(g_task_runner);
  return instance;
}

Sched* sched() {
  static Sched* instance = new Sched(g_task_runner, blob_reader());
  return instance;
}

}  // namespace

// Functions exported to JS.

extern "C" {
void EMSCRIPTEN_KEEPALIVE Initialize(TraceID, ReadTraceFunction, ReplyFunction);
void Initialize(TraceID trace_id,
                ReadTraceFunction read_trace_function,
                ReplyFunction reply_function) {
  printf("Initializing WASM bridge\n");
  g_trace_id = trace_id;
  g_task_runner = new EmscriptenTaskRunner();
  g_read_trace = read_trace_function;
  g_reply = reply_function;
  PERFETTO_CHECK(!g_trace_processor);
  g_trace_processor = new TraceProcessor(nullptr, nullptr);
}

void EMSCRIPTEN_KEEPALIVE ReadComplete(uint32_t, const uint8_t*, uint32_t);
void ReadComplete(uint32_t offset, const uint8_t* data, uint32_t size) {
  g_read_callback(offset, data, size);
}

// Here we have one function for each method of each RPC service defined in
// trace_processor/*.proto.

// TODO(primiano): autogenerate these.
void EMSCRIPTEN_KEEPALIVE sched_getSchedEvents(RequestID, const uint8_t*, int);
void sched_getSchedEvents(RequestID req_id,
                          const uint8_t* query_data,
                          int len) {
  protos::Query query;
  bool parsed = query.ParseFromArray(query_data, len);
  if (!parsed) {
    std::string err = "Failed to parse input request";
    g_reply(req_id, false, err.data(), err.size());
  }

  // When the C++ class implementing the service replies, serialize the protobuf
  // result and post it back to the worker script (|g_reply|).
  auto callback = [req_id](const protos::SchedEvent& events) {
    std::string encoded;
    events.SerializeToString(&encoded);
    g_reply(req_id, true, encoded.data(),
            static_cast<uint32_t>(encoded.size()));
  };

  sched()->GetSchedEvents(query, callback);
}

}  // extern "C"

}  // namespace trace_processor
}  // namespace perfetto
