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

#ifndef SRC_TRACE_PROCESSOR_QUERY_UTILS_H_
#define SRC_TRACE_PROCESSOR_QUERY_UTILS_H_

#include <inttypes.h>
#include <sqlite3.h>

#include "src/trace_processor/scoped_db.h"
#include "src/trace_processor/table.h"

namespace perfetto {
namespace trace_processor {
namespace query_utils {

inline std::vector<Table::Column> GetColumnsForTable(
    sqlite3* db,
    const std::string& raw_table_name) {
  char sql[1024];
  const char kRawSql[] = "SELECT name, type from pragma_table_info(\"%s\")";

  // Support names which are table valued functions with arguments.
  std::string table_name = raw_table_name.substr(0, raw_table_name.find('('));
  int n = snprintf(sql, sizeof(sql), kRawSql, table_name.c_str());
  PERFETTO_DCHECK(n >= 0 || static_cast<size_t>(n) < sizeof(sql));

  sqlite3_stmt* raw_stmt = nullptr;
  int err = sqlite3_prepare_v2(db, sql, n, &raw_stmt, nullptr);

  ScopedStmt stmt(raw_stmt);
  PERFETTO_DCHECK(sqlite3_column_count(*stmt) == 2);

  std::vector<Table::Column> columns;
  while (true) {
    err = sqlite3_step(raw_stmt);
    if (err == SQLITE_DONE)
      break;
    if (err != SQLITE_ROW) {
      PERFETTO_ELOG("Querying schema of table failed");
      return {};
    }

    const char* name =
        reinterpret_cast<const char*>(sqlite3_column_text(*stmt, 0));
    const char* raw_type =
        reinterpret_cast<const char*>(sqlite3_column_text(*stmt, 1));
    if (!name || !raw_type || !*name || !*raw_type) {
      PERFETTO_ELOG("Schema has invalid column values");
      return {};
    }

    Table::ColumnType type;
    if (strcmp(raw_type, "UNSIGNED BIG INT") == 0) {
      type = Table::ColumnType::kUlong;
    } else if (strcmp(raw_type, "UNSIGNED INT") == 0) {
      type = Table::ColumnType::kUint;
    } else if (strcmp(raw_type, "STRING") == 0) {
      type = Table::ColumnType::kString;
    } else {
      PERFETTO_FATAL("Unknown column type on table %s", raw_table_name.c_str());
    }
    columns.emplace_back(columns.size(), name, type);
  }
  return columns;
}

inline bool IsCountOfTableBelow(sqlite3* db,
                                const std::string& table,
                                uint64_t max_count) {
  char sql[1024];
  const char kRawSql[] =
      "SELECT COUNT(*) FROM (SELECT 1 from %s LIMIT %" PRIu64 ");";

  int n = snprintf(sql, sizeof(sql), kRawSql, table.c_str(), max_count);

  sqlite3_stmt* raw_stmt = nullptr;
  int err = sqlite3_prepare_v2(db, sql, n, &raw_stmt, nullptr);
  ScopedStmt stmt(raw_stmt);
  if (err != SQLITE_OK)
    return false;
  PERFETTO_DCHECK(sqlite3_column_count(raw_stmt) == 1);

  err = sqlite3_step(raw_stmt);
  if (err != SQLITE_ROW)
    return false;

  uint64_t count = static_cast<uint64_t>(sqlite3_column_int64(raw_stmt, 0));
  PERFETTO_DCHECK(sqlite3_step(raw_stmt) == SQLITE_DONE);
  return count < max_count;
}

}  // namespace query_utils
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_QUERY_UTILS_H_
