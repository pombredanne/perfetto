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

#include "src/trace_processor/trace_storage.h"

#include <string.h>

#include <type_traits>

namespace perfetto {
namespace trace_processor {

TraceStorage::TraceStorage() {
  // Upid/utid 0 is reserved for invalid processes/threads.
  unique_processes_.emplace_back(0);
  unique_threads_.emplace_back(0);
}

TraceStorage::~TraceStorage() {}

TraceStorage& TraceStorage::operator=(TraceStorage&&) noexcept = default;

void TraceStorage::AddSliceToCpu(uint32_t cpu,
                                 uint64_t start_ns,
                                 uint64_t duration_ns,
                                 UniqueTid utid) {
  cpu_events_[cpu].AddSlice(start_ns, duration_ns, utid);
};


void TraceStorage::ResetStorage() {
  *this = TraceStorage();
}

}  // namespace trace_processor
}  // namespace perfetto
