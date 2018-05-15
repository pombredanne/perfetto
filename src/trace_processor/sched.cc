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

#include "perfetto/trace_processor/sched.h"

#include <algorithm>
#include <numeric>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/trace_processor/blob_reader.h"
#include "perfetto/trace_processor/query.pb.h"
#include "perfetto/trace_processor/sched.pb.h"

namespace perfetto {
namespace trace_processor {

namespace {

constexpr uint32_t kNumBlocks = 1000;
constexpr uint32_t kBlockSize = 128 * 1024;

base::TimeMillis t_start{};
base::TimeMillis t_last_req{};
BlobReader* g_reader;

void OnReadComplete(uint32_t off, const uint8_t*, size_t len) {
  auto now = base::GetWallTimeMs();
  static std::vector<int>* lat = new std::vector<int>;
  lat->push_back(static_cast<int>((now - t_last_req).count()));

  static uint32_t bytes_read = 0;
  static uint32_t blocks_read = 0;
  PERFETTO_DCHECK(off == bytes_read);
  bytes_read += len;
  if (++blocks_read >= kNumBlocks) {
    int ms = static_cast<int>((now - t_start).count());

    PERFETTO_ILOG("ReadCompelte %u KB (%u blocks) in %d ms. %.2f KB/s",
                  bytes_read / 1024, blocks_read, ms,
                  bytes_read * 1000.0 / ms / 1024);
    PERFETTO_ILOG(
        "Latency RTT (C++ -> JS -> C++) [ms.]: min: %d, max: %d, avg: "
        "%.3lf",
        *std::min_element(lat->begin(), lat->end()),
        *std::max_element(lat->begin(), lat->end()),
        std::accumulate(lat->begin(), lat->end(), 0) /
            static_cast<double>(lat->size()));
  } else {
    t_last_req = base::GetWallTimeMs();
    g_reader->Read(bytes_read, kBlockSize, OnReadComplete);
  }
}

}  // namespace

Sched::Sched(base::TaskRunner* task_runner, BlobReader* reader)
    : task_runner_(task_runner), reader_(reader) {}

void Sched::GetSchedEvents(const protos::Query&,
                           GetSchedEventsCallback callback) {
  // TODO(primiano): This function should access the underlying storage, execute
  // the right query and return the results back using the |callback|. Right now
  // let's just cheat for the sake of the prototype.
  task_runner_->PostTask([callback] {
    protos::SchedEvents events;
    protos::SchedEvent* evt = events.add_events();
    evt->set_process_name("com.foo.bar");
    evt = events.add_events();
    evt->set_process_name("com.foo.baz");
    callback(events);
  });

  t_last_req = t_start = base::GetWallTimeMs();
  g_reader = reader_;
  reader_->Read(0, kBlockSize, OnReadComplete);
}

void Sched::GetQuantizedSchedActivity(const protos::Query&,
                                      GetQuantizedSchedActivityCallback) {
  // TODO(primiano): same here.
}

}  // namespace trace_processor
}  // namespace perfetto
