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

#include "src/trace_processor/time_tracker.h"

#include <algorithm>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {

TimeTracker::TimeTracker() = default;
TimeTracker::~TimeTracker() = default;

void TimeTracker::PushClockSnapshot(ClockDomain domain,
                                    int64_t clock_time_ns,
                                    int64_t trace_time_ns) {
  PERFETTO_DCHECK(domain < clocks_.size());
  ClockSnapshotVector& snapshots = clocks_[domain];
  PERFETTO_DCHECK(snapshots.empty() ||
                  (snapshots.back().clock_time_ns <= clock_time_ns &&
                   snapshots.back().trace_time_ns <= trace_time_ns));
  snapshots.emplace_back(ClockSnapshot{clock_time_ns, trace_time_ns});
}

int64_t TimeTracker::ToTraceTime(ClockDomain domain, int64_t clock_time_ns) {
  ClockSnapshotVector& snapshots = clocks_[domain];
  if (snapshots.empty()) {
    PERFETTO_DCHECK(false);
    return clock_time_ns;
  }
  static auto comparator = [](int64_t lhs, const ClockSnapshot& rhs) {
    return lhs < rhs.clock_time_ns;
  };
  auto it = std::upper_bound(snapshots.begin(), snapshots.end(), clock_time_ns,
                             comparator);
  if (it != snapshots.begin())
    it--;
  return it->trace_time_ns + (clock_time_ns - it->clock_time_ns);
}

}  // namespace trace_processor
}  // namespace perfetto
