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

#ifndef SRC_TRACE_PROCESSOR_TRACE_DATABASE_H_
#define SRC_TRACE_PROCESSOR_TRACE_DATABASE_H_

#include <memory>

#include "sqlite3.h"
#include "src/trace_processor/sched_slice_table.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class TraceDatabase {
 public:
  TraceDatabase(TraceStorage* storage);
  ~TraceDatabase();

 private:
  static TraceDatabase* GetDatabase(sqlite3* db);
  static SchedSliceTable* GetSchedSliceTable(sqlite3_vtab* vtab);

  sqlite3* db_ = nullptr;  // must be first.
  TraceStorage* storage_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TRACE_DATABASE_H_
