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

#ifndef SRC_TRACE_PROCESSOR_SPAN_TABLE_H_
#define SRC_TRACE_PROCESSOR_SPAN_TABLE_H_

#include <sqlite3.h>
#include <array>
#include <deque>
#include <memory>

#include "src/trace_processor/scoped_db.h"
#include "src/trace_processor/table.h"

namespace perfetto {
namespace trace_processor {

class SpanTable : public Table {
 public:
  struct Column {
    std::string name;
    std::string type;
  };

  SpanTable(sqlite3*, const TraceStorage*);

  static void RegisterTable(sqlite3* db, const TraceStorage* storage);

  // Table implementation.
  std::string CreateTableStmt(int argc, const char* const* argv) override;
  std::unique_ptr<Table::Cursor> CreateCursor() override;
  int BestIndex(const QueryConstraints& qc, BestIndexInfo* info) override;

 private:
  static constexpr uint8_t kReservedColumns = 3;
  struct Value {
    enum Type {
      kText = 0,
      kULong = 1,
      kUInt = 2,
    };

    Type type;
    std::string text_value;
    uint64_t ulong_value;
    uint32_t uint_value;
  };

  class FilterState {
   public:
    FilterState(SpanTable*, sqlite3_stmt* t1_stmt, sqlite3_stmt* t2_stmt);

    int Next();
    int Eof();
    int Column(sqlite3_context* context, int N);

   private:
    struct TableRow {
      uint64_t ts = 0;
      uint64_t dur = 0;
      std::vector<Value> values;
    };

    int ExtractNext(bool pull_t1);
    void ReportSqliteResult(sqlite3_context* context, SpanTable::Value value);

    uint64_t ts_ = 0;
    uint64_t dur_ = 0;
    uint32_t cpu_ = 0;
    TableRow t1_to_ret_;
    TableRow t2_to_ret_;

    std::array<TableRow, base::kMaxCpus> t1_;
    std::array<TableRow, base::kMaxCpus> t2_;

    uint64_t latest_t1_ts_ = 0;
    uint64_t latest_t2_ts_ = 0;

    ScopedStmt t1_stmt_;
    ScopedStmt t2_stmt_;

    SpanTable* const table_;
  };

  class Cursor : public Table::Cursor {
   public:
    Cursor(SpanTable*, sqlite3* db);
    ~Cursor() override;

    // Methods to be implemented by derived table classes.
    int Filter(const QueryConstraints& qc, sqlite3_value** argv) override;
    int Next() override;
    int Eof() override;
    int Column(sqlite3_context* context, int N) override;

   private:
    sqlite3* const db_;
    std::unique_ptr<FilterState> filter_state_;
    SpanTable* const table_;
  };

  struct TableDefenition {
    std::string name;
    std::vector<Column> cols;
  };

  TableDefenition t1_;
  TableDefenition t2_;
  sqlite3* const db_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_SPAN_TABLE_H_
