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

#ifndef SRC_TRACE_PROCESSOR_COUNTERS_TABLE_H_
#define SRC_TRACE_PROCESSOR_COUNTERS_TABLE_H_

#include <limits>
#include <memory>

#include "src/trace_processor/table.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class CountersTable : public Table {
 public:
  enum Column {
    kTimestamp = 0,
    kName = 1,
    kValue = 2,
    kDuration = 3,
    kRef = 4,
  };

  static void RegisterTable(sqlite3* db,
                            const TraceStorage* storage,
                            std::string name);

  CountersTable(const TraceStorage*);

  // Table implementation.
  std::unique_ptr<Table::Cursor> CreateCursor() override;
  int BestIndex(const QueryConstraints&, BestIndexInfo*) override;

 private:
  class Cursor : public Table::Cursor {
   public:
    Cursor(const TraceStorage*, std::string name);

    // Implementation of Table::Cursor.
    int Filter(const QueryConstraints&, sqlite3_value**) override;
    int Next() override;
    int Eof() override;
    int Column(sqlite3_context*, int N) override;

   private:
    struct RefFilter {
      uint32_t min;
      uint32_t max;
      uint32_t current;
      bool desc;
    };

    RefFilter ref_filter_;
    CounterType type_;
    size_t index_ = 0;

    const TraceStorage* const storage_;
  };

  const TraceStorage* const storage_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_COUNTERS_TABLE_H_
