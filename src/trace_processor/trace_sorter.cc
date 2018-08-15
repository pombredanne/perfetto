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
#include <utility>

#include "src/trace_processor/proto_trace_parser.h"
#include "src/trace_processor/trace_sorter.h"

namespace perfetto {
namespace trace_processor {

TraceSorter::TraceSorter(TraceProcessorContext* context,
                         uint64_t window_size_ns)
    : context_(context), window_size_ns_(window_size_ns){};

void TraceSorter::PushTracePacket(uint64_t timestamp,
                                  TraceBlobView trace_view) {
  TimestampedTracePiece ttp(
      std::move(trace_view), false /* is_ftrace */,
      0 /* cpu - this field should never be used for non-ftrace packets */);
  events_.emplace(timestamp, ttp);
  MaybeFlushEvents(false);
}

void TraceSorter::PushFtracePacket(uint32_t cpu,
                                   uint64_t timestamp,
                                   TraceBlobView trace_view) {
  TimestampedTracePiece ttp(std::move(trace_view), true /* is_ftrace */, cpu);
  events_.emplace(timestamp, ttp);
  MaybeFlushEvents(false);
}

void TraceSorter::MaybeFlushEvents(bool force_flush) {
  while (!events_.empty()) {
    uint64_t oldest_timestamp = events_.begin()->first;
    uint64_t most_recent_timestamp = events_.rbegin()->first;

    // Only flush if there is an event older than the window size or
    // if we are force flushing.
    if (most_recent_timestamp - oldest_timestamp < window_size_ns_ &&
        !force_flush)
      break;

    auto oldest = events_.begin();
    if (oldest->second.is_ftrace) {
      context_->parser->ParseFtracePacket(oldest->second.cpu,
                                          oldest->first /*timestamp*/,
                                          oldest->second.blob_view);
    } else {
      context_->parser->ParseTracePacket(oldest->second.blob_view);
    }
    events_.erase(oldest);
  }
}

}  // namespace trace_processor
}  // namespace perfetto
