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

#include <algorithm>
#include <utility>

#include "src/trace_processor/proto_trace_parser.h"
#include "src/trace_processor/trace_sorter.h"

namespace perfetto {
namespace trace_processor {

namespace {

inline void MoveToTraceParser(ProtoTraceParser* proto_parser,
                              TraceSorter::TimestampedTracePiece* ttp) {
  TraceBlobView tbv = std::move(ttp->blob_view);
  if (ttp->is_ftrace()) {
    proto_parser->ParseFtracePacket(ttp->cpu, ttp->timestamp, std::move(tbv));
  } else {
    proto_parser->ParseTracePacket(std::move(tbv));
  }
}

}  // namespace

TraceSorter::TraceSorter(TraceProcessorContext* context,
                         OptimizationMode optimization,
                         uint64_t window_size_ns)
    : context_(context),
      window_size_ns_(window_size_ns),
      optimization_(optimization) {}

void TraceSorter::SortAndFlushEventsBeyondWindow(uint64_t window_size_ns) {
  if (sort_start_idx_ > 0) {
    PERFETTO_DCHECK(sort_start_idx_ < events_.size());
    PERFETTO_DCHECK(sort_min_ts_ > 0 && sort_min_ts_ < latest_timestamp_);

    // We know that all events between [0, sort_start_idx_] are sorted. Witin
    // this range, perform a bound search and find the iterator for the min
    // timestamp that broke the monotonicity. Re-sort from there to the end.
    PERFETTO_DCHECK(std::is_sorted(
        events_.begin(),
        events_.begin() + static_cast<ssize_t>(sort_start_idx_)));
    auto sort_from = std::lower_bound(
        events_.begin(),
        events_.begin() + static_cast<ssize_t>(sort_start_idx_), sort_min_ts_,
        &TimestampedTracePiece::Compare);
    std::sort(sort_from, events_.end());
    sort_start_idx_ = 0;
    sort_min_ts_ = 0;
  }

  // At this point |events_| is fully sorted again.
  PERFETTO_DCHECK(std::is_sorted(events_.begin(), events_.end()));

  if (PERFETTO_UNLIKELY(latest_timestamp_ < window_size_ns))
    return;

  // Now that all events are sorted, flush all events in the range
  // [earlierst_timestamp .. latest_timestamp - window_size_ns].
  auto flush_end_it = std::lower_bound(events_.begin(), events_.end(),
                                       1 + latest_timestamp_ - window_size_ns,
                                       &TimestampedTracePiece::Compare);

  for (auto it = events_.begin(); it != flush_end_it; it++) {
    uint64_t cur_timestamp = it->timestamp;
    PERFETTO_DCHECK(latest_timestamp_ - cur_timestamp >= window_size_ns);
    MoveToTraceParser(context_->proto_parser.get(), &*it);
  }

  events_.erase(events_.begin(), flush_end_it);

  if (events_.size() > 0) {
    earliest_timestamp_ = events_.front().timestamp;
    latest_timestamp_ = events_.back().timestamp;
  } else {
    earliest_timestamp_ = std::numeric_limits<uint64_t>::max();
    latest_timestamp_ = 0;
  }
}

}  // namespace trace_processor
}  // namespace perfetto
