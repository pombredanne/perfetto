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
    : parser_(reader, &storage_, kTraceChunkSizeB),
      task_runner_(task_runner),
      weak_factory_(this) {
  sqlite3_open(":memory:", &db_);

  // Setup the sched slice table.
  sqlite3_module module = SchedSliceTable::CreateModule();
  sqlite3_create_module(db_, "sched_slice", &module,
                        static_cast<void*>(&storage_));
  LoadTraceChunk();
}

TraceDatabase::~TraceDatabase() {
  sqlite3_close(db_);
}

void TraceDatabase::LoadTraceChunk() {
  parser_.ParseNextChunk();
  auto ptr = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([ptr] {
    if (!ptr)
      return;
    ptr->LoadTraceChunk();
  });
}

}  // namespace trace_processor
}  // namespace perfetto
