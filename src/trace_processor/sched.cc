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

constexpr uint32_t kBlockSize = 1024 * 1024;

base::TimeMillis t_start{};

}  // namespace

Sched::Sched(base::TaskRunner* task_runner, BlobReader* reader)
    : task_runner_(task_runner),
      reader_(reader),
      buf_(new uint8_t[kBlockSize]) {}

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

  bytes_read_ = 0;
  task_runner_->PostTask([this] { DoRead(); });
}

void Sched::DoRead() {
  for (;;) {
    uint32_t len = reader_->Read(bytes_read_, kBlockSize, buf_.get());
    bytes_read_ += len;
    if (len == kBlockSize)
      continue;
    int ms = static_cast<int>((base::GetWallTimeMs() - t_start).count());
    PERFETTO_ILOG("ReadCompelte %u KBin %d ms. %.2f KB/s", bytes_read_ / 1024,
                  ms, bytes_read_ * 1000.0 / ms / 1024);
    break;
  }
}

void Sched::GetQuantizedSchedActivity(const protos::Query&,
                                      GetQuantizedSchedActivityCallback) {
  // TODO(primiano): same here.
}

}  // namespace trace_processor
}  // namespace perfetto
