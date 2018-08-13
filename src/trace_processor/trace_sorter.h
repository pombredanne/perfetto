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

#ifndef SRC_TRACE_PROCESSOR_TRACE_SORTER_H_
#define SRC_TRACE_PROCESSOR_TRACE_SORTER_H_

#include "src/trace_processor/trace_blob_view.h"
#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

// Events from the trace come into this class ordered per cpu. This class stores
// the events for |window_size_ms| ms and then outputs all the collected events
// in the correct global order.
class TraceSorter {
 public:
  TraceSorter(TraceProcessorContext*, uint64_t window_size_ms);

  void PushTracePacket(uint64_t timestamp, TraceBlobView);
  void PushFtracePacket(uint32_t cpu, uint64_t timestamp, TraceBlobView);

  // When the file is fully parsed, all remaining events will be flushed.
  void NotifyEOF();

  // For testing.
  void set_window_ms(uint64_t window_size_ms) {
    window_size_ms_ = window_size_ms;
  }

 private:
  struct TimestampedTracePiece {
    TraceBlobView blob_view;
    bool is_ftrace;
    uint32_t cpu;
  };

  TraceProcessorContext* context_;
  uint64_t window_size_ms_;

  // All events, with the oldest at the beginning.
  std::map<uint64_t /* timestamp */, TimestampedTracePiece> events_;

  // This method passes any events older than window_size_ms to the
  // parser to be parsed and then stored.
  void FlushEvents();

  // Returns true if the difference between the timestamp of the oldest and
  // most recent events is larger than the window size. This means the oldest
  // event should be flushed.
  bool EventReadyToFlush();
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TRACE_SORTER_H_
