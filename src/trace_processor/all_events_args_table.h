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

#ifndef SRC_TRACE_PROCESSOR_ALL_EVENTS_ARGS_TABLE_H_
#define SRC_TRACE_PROCESSOR_ALL_EVENTS_ARGS_TABLE_H_

#include <limits>
#include <memory>

#include "src/trace_processor/filtered_row_index.h"
#include "src/trace_processor/table.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class AllEventsArgsTable : public Table {
 public:
  enum Column {
    kRowId = 0,
    kFlatKey = 1,
    kKey = 2,
    kIntValue = 3,
    kStringValue = 4,
    kRealValue = 5
  };

  static void RegisterTable(sqlite3* db, const TraceStorage* storage);

  AllEventsArgsTable(sqlite3*, const TraceStorage*);

  // Table implementation.
  base::Optional<Table::Schema> Init(int, const char* const*) override;
  std::unique_ptr<Table::Cursor> CreateCursor(const QueryConstraints&,
                                              sqlite3_value**) override;
  int BestIndex(const QueryConstraints&, BestIndexInfo*) override;

 private:
  enum SchedField : uint32_t {
    kPrevComm = 0,
    kPrevPid,
    kPrevPrio,
    kPrevState,
    kNextComm,
    kNextPid,
    kNextPrio,
    kMax
  };

  class Cursor : public Table::Cursor {
   public:
    Cursor(const TraceStorage*, const QueryConstraints&, sqlite3_value**);

    // Implementation of Table::Cursor.
    int Next() override;
    int Eof() override;
    int Column(sqlite3_context*, int N) override;

   private:
    enum class Type { kArgs = 0, kSched };

    int ColumnSched(sqlite3_context*, int N);

    Type type_ = Type::kArgs;
    std::unique_ptr<RowIterator> args_it_;
    std::unique_ptr<RowIterator> sched_it_;

    const TraceStorage* const storage_;
  };

  const TraceStorage* const storage_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_ALL_EVENTS_ARGS_TABLE_H_
