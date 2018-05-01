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

#include "perfetto/base/task_runner.h"

#include <inttypes.h>

namespace perfetto {
namespace base {

#if PERFETTO_DCHECK_IS_ON()
void TaskRunner::AddToHistogram(base::TimeMillis value) {
  base::TimeMillis cur = base::TimeMillis(-1);
  for (auto& p : delay_histogram_ms_) {
    if (cur < value && value <= p.first) {
      p.second++;
      return;
    }
    cur = p.first;
  }
  PERFETTO_CHECK(false);
}
#endif

void TaskRunner::PrintDebugInfo() {
#if PERFETTO_DCHECK_IS_ON()
  base::TimeMillis cur = base::TimeMillis(-1);
  PERFETTO_DLOG("TaskRunner delays:");
  for (auto& p : delay_histogram_ms_) {
    PERFETTO_DLOG("(%" PRId64 ", %" PRId64 "]: %" PRIu64, int64_t(cur.count()),
                  int64_t(p.first.count()), p.second);
    cur = p.first;
  }
#endif
}
}
}  // namespace perfetto
