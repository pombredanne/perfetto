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

std::set<std::string> GetColumnNamesForTable(sqlite3* db,
                                             const std::string& table_name) {
  char sql[100];
  const char kRawSql[] = "SELECT name from pragma_table_info(\"%s\")";

  int n = snprintf(sql, sizeof(sql), kRawSql, table_name.c_str());
  if (n < 0 || static_cast<size_t>(n) >= sizeof(sql)) {
    return {};
  }

  sqlite3_stmt* raw_stmt;
  int err = sqlite3_prepare_v2(db, sql, n, &raw_stmt, nullptr);
  ScopedStmt stmt(raw_stmt);

  int col_count = sqlite3_column_count(*stmt);
  if (col_count != 1) {
    return {};
  }

  std::set<std::string> column_names;
  while (!err) {
    err = sqlite3_step(raw_stmt);
    if (err == SQLITE_DONE)
      break;
    if (err != SQLITE_ROW)
      return {};

    const auto* name = sqlite3_column_text(*stmt, 0);
    if (!name)
      return {};
    column_names.emplace(name);
  }
  return column_names;
}

bool HasColumn(const std::string& name,
               const std::set<std::string>& first,
               const std::set<std::string>& second) {
  return first.find(name) != first.end() && second.find(name) != second.end();
}

}  // namespace

SpanTable::SpanTable(sqlite3* db, const TraceStorage*) : db_(db) {}

void SpanTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<SpanTable>(db, storage, "SPAN");
}

std::string SpanTable::CreateTableStmt(int argc, const char* const* argv) {
  if (argc < 3) {
    return "";
  }

  const char* table_one = argv[0];
  const char* table_two = argv[1];
  const char* merge_col = argv[2];

  auto cols_table_one = GetColumnNamesForTable(db_, table_one);
  auto cols_table_two = GetColumnNamesForTable(db_, table_two);

  if (!HasColumn(merge_col, cols_table_one, cols_table_two)) {
    return "";
  } else if (!HasColumn("ts", cols_table_one, cols_table_two)) {
    return "";
  } else if (!HasColumn("dur", cols_table_one, cols_table_two)) {
    return "";
  }
}

}  // namespace trace_processor
}  // namespace perfetto
