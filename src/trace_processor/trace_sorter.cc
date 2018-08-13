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
#include "src/trace_processor/trace_sorter.h"
#include "src/trace_processor/proto_trace_parser.h"

namespace perfetto {
namespace trace_processor {

TraceSorter::TraceSorter(TraceProcessorContext* context,
                         uint64_t window_size_ms)
    : context_(context), window_size_ms_(window_size_ms){};

void TraceSorter::PushTracePacket(uint64_t timestamp,
                                  TraceBlobView trace_view) {
  TimestampedTracePiece ttp;
  ttp.is_ftrace = false;
  ttp.blob_view = trace_view;
  events_.emplace(timestamp, ttp);
  FlushEvents();
}

void TraceSorter::PushFtracePacket(uint32_t cpu,
                                   uint64_t timestamp,
                                   TraceBlobView trace_view) {
  TimestampedTracePiece ttp;
  ttp.is_ftrace = true;
  ttp.cpu = cpu;
  ttp.blob_view = trace_view;
  events_.emplace(timestamp, ttp);
  FlushEvents();
}

void TraceSorter::FlushEvents() {
  auto it = events_.begin();
  while (it != events_.end() &&
         it->first - (events_.rbegin()->first) >= (window_size_ms_ * 1000000)) {
    if (it->second.is_ftrace) {
      context_->parser->ParseFtracePacket(
          it->second.cpu, it->first /*timestamp*/, it->second.blob_view);
    } else {
      context_->parser->ParseTracePacket(it->second.blob_view);
    }
    events_.erase(it);
    it = events_.begin();
  }
}

void TraceSorter::NotifyEOF() {
  for (auto it = events_.begin(); it != events_.end(); ++it) {
    if (it->second.is_ftrace) {
      context_->parser->ParseFtracePacket(
          it->second.cpu, it->first /*timestamp*/, it->second.blob_view);
    } else {
      context_->parser->ParseTracePacket(it->second.blob_view);
    }
  }
  events_.clear();
}

}  // namespace trace_processor
}  // namespace perfetto
