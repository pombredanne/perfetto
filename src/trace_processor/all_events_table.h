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

#ifndef SRC_TRACE_PROCESSOR_ALL_EVENTS_TABLE_H_
#define SRC_TRACE_PROCESSOR_ALL_EVENTS_TABLE_H_

#include <limits>
#include <memory>

#include "src/trace_processor/row_iterators.h"
#include "src/trace_processor/table.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

// The implementation of the SQLite table containing each unique process with
// their details (only name at the moment).
class AllEventsTable : public Table {
 public:
  enum Column { kId = 0, kTs = 1, kName = 2, kCpu = 3, kUtid = 4 };

  static void RegisterTable(sqlite3* db, const TraceStorage* storage);

  AllEventsTable(sqlite3*, const TraceStorage*);

  // Table implementation.
  base::Optional<Table::Schema> Init(int, const char* const*) override;
  std::unique_ptr<Table::Cursor> CreateCursor(const QueryConstraints&,
                                              sqlite3_value**) override;
  int BestIndex(const QueryConstraints&, BestIndexInfo*) override;

 private:
  class Cursor : public Table::Cursor {
   public:
    Cursor(const TraceStorage*, const QueryConstraints&, sqlite3_value**);

    // Implementation of Table::Cursor.
    int Next() override;
    int Eof() override;
    int Column(sqlite3_context*, int N) override;

   private:
    enum class Type { kRaw = 0, kSched };

    Type type_ = Type::kRaw;
    std::unique_ptr<RowIterator> raw_it_;
    std::unique_ptr<RowIterator> sched_it_;

    const TraceStorage* const storage_;
  };

  int64_t Timestamp(TableId table_id, uint32_t row) {
    const auto& raw = storage_->raw_events();
    const auto& sched = storage_->slices();
    return table_id == TableId::kRawEvents ? raw.timestamps()[row]
                                           : sched.start_ns()[row];
  }

  uint32_t Cpu(TableId table_id, uint32_t row) {
    const auto& raw = storage_->raw_events();
    const auto& sched = storage_->slices();
    return table_id == TableId::kRawEvents ? raw.cpus()[row]
                                           : sched.cpus()[row];
  }

  UniqueTid Utid(TableId table_id, uint32_t row) {
    const auto& raw = storage_->raw_events();
    const auto& sched = storage_->slices();
    return table_id == TableId::kRawEvents ? raw.utids()[row]
                                           : sched.utids()[row];
  }

  int FormatSystraceArgs(TableId table_id, uint32_t row, char* line, size_t n);

  void ToSystrace(sqlite3_context* ctx, int argc, sqlite3_value** argv);

  const TraceStorage* const storage_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_ALL_EVENTS_TABLE_H_
