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

#include "src/trace_processor/span_table.h"

#include <sqlite3.h>
#include <string.h>
#include <set>

#include "perfetto/base/logging.h"
#include "src/trace_processor/scoped_db.h"

namespace perfetto {
namespace trace_processor {

namespace {

bool ColumnCompare(const SpanTable::Column& first,
                   const SpanTable::Column& second) {
  return first.name < second.name;
}

std::vector<SpanTable::Column> GetColumnsForTable(
    sqlite3* db,
    const std::string& table_name) {
  char sql[100];
  const char kRawSql[] = "SELECT name, type from pragma_table_info(\"%s\")";

  int n = snprintf(sql, sizeof(sql), kRawSql, table_name.c_str());
  if (n < 0 || static_cast<size_t>(n) >= sizeof(sql))
    return {};

  sqlite3_stmt* raw_stmt;
  int err = sqlite3_prepare_v2(db, sql, n, &raw_stmt, nullptr);
  ScopedStmt stmt(raw_stmt);

  int col_count = sqlite3_column_count(*stmt);
  if (col_count != 2) {
    return {};
  }

  std::vector<SpanTable::Column> columns;
  while (!err) {
    err = sqlite3_step(raw_stmt);
    if (err == SQLITE_DONE)
      break;
    if (err != SQLITE_ROW)
      return {};

    auto* name = sqlite3_column_text(*stmt, 0);
    auto* type = sqlite3_column_text(*stmt, 1);

    if (!name || !type)
      return {};
    SpanTable::Column column;
    column.name = reinterpret_cast<const char*>(name);
    column.type = reinterpret_cast<const char*>(type);
    columns.emplace_back(column);
  }
  std::sort(columns.begin(), columns.end(), &ColumnCompare);
  return columns;
}

}  // namespace

SpanTable::SpanTable(sqlite3* db, const TraceStorage*) : db_(db) {}

void SpanTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<SpanTable>(db, storage, "SPAN");
}

std::string SpanTable::CreateTableStmt(int argc, const char* const* argv) {
  if (argc < 3)
    return "";

  t1_cols_ = GetColumnsForTable(db_, argv[0]);
  t2_cols_ = GetColumnsForTable(db_, argv[1]);

  std::vector<std::string> intersection;
  std::set_intersection(t1_cols_.begin(), t1_cols_.end(), t2_cols_.begin(),
                        t2_cols_.end(), std::back_inserter(intersection),
                        &ColumnCompare);

  std::string merge_col = argv[2];

  // TODO(tilal6991): check that dur, ts and merge_col are all present in
  // the tables and have the same type.
  bool is_valid = intersection.size() == 3;
  if (!is_valid)
    return "";

  return "CREATE TABLE x(ts UNSIGNED BIG INT, dur UNSIGNED BIG INT, cpu "
         "UNSIGNED INT)";
}

std::unique_ptr<Table::Cursor> SpanTable::CreateCursor() {
  return std::unique_ptr<SpanTable::Cursor>(new SpanTable::Cursor());
}

int SpanTable::BestIndex(const QueryConstraints& qc, BestIndexInfo* info) {}

}  // namespace trace_processor
}  // namespace perfetto
