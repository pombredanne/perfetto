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

#include "src/trace_processor/trace_database.h"

namespace perfetto {
namespace trace_processor {
namespace {
constexpr uint32_t kTraceChunkSizeB = 16 * 1024 * 1024;  // 16 MB
}

TraceDatabase::TraceDatabase(BlobReader* reader, base::TaskRunner* task_runner)
    : parser_(reader, &storage_, kTraceChunkSizeB), task_runner_(task_runner) {
  sqlite3_open(":memory:", &db_);

  // Setup the sched slice table.
  sqlite3_module module = SchedSliceTable::CreateModule();
  SchedSliceTable::Args* args = new SchedSliceTable::Args();
  args->storage = &storage_;
  sqlite3_create_module_v2(db_, "sched_slice", &module, args, [](void* a) {
    delete reinterpret_cast<SchedSliceTable::Args*>(a);
  });
  LoadTraceChunk();
}

TraceDatabase::~TraceDatabase() {
  sqlite3_close(db_);
}

void TraceDatabase::LoadTraceChunk() {
  parser_.ParseNextChunk();
  task_runner_->PostTask(std::bind(&TraceDatabase::LoadTraceChunk, this));
}

}  // namespace trace_processor
}  // namespace perfetto
