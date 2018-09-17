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
#include <sstream>

#include "perfetto/base/logging.h"

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
  const char kRawSql[] = "SELECT name, type from pragma_table_info(%s)";

  int n = snprintf(sql, sizeof(sql), kRawSql, table_name.c_str());
  PERFETTO_DCHECK(n >= 0 || static_cast<size_t>(n) < sizeof(sql));

  sqlite3_stmt* raw_stmt;
  int err = sqlite3_prepare_v2(db, sql, n, &raw_stmt, nullptr);

  ScopedStmt stmt(raw_stmt);
  int col_count = sqlite3_column_count(*stmt);
  PERFETTO_DCHECK(col_count == 2);

  std::vector<SpanTable::Column> columns;
  while (true) {
    err = sqlite3_step(raw_stmt);
    if (err == SQLITE_DONE)
      break;
    if (err != SQLITE_ROW) {
      PERFETTO_DCHECK(false);
      return {};
    }

    auto* name = sqlite3_column_text(*stmt, 0);
    auto* type = sqlite3_column_text(*stmt, 1);
    PERFETTO_DCHECK(name && type);

    if (!name || !type)
      return {};
    SpanTable::Column column;
    column.name = reinterpret_cast<const char*>(name);
    column.type = reinterpret_cast<const char*>(type);

    std::transform(column.type.begin(), column.type.end(), column.type.begin(),
                   ::toupper);
    columns.emplace_back(column);
  }
  std::sort(columns.begin(), columns.end(), &ColumnCompare);
  return columns;
}

}  // namespace

SpanTable::SpanTable(sqlite3* db, const TraceStorage*) : db_(db) {}

void SpanTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<SpanTable>(db, storage, "span");
}

std::string SpanTable::CreateTableStmt(int argc, const char* const* argv) {
  if (argc < 6)
    return "";

  t1_.name = reinterpret_cast<const char*>(argv[3]);
  t1_.cols = GetColumnsForTable(db_, t1_.name);

  t2_.name = reinterpret_cast<const char*>(argv[4]);
  t2_.cols = GetColumnsForTable(db_, t2_.name);

  // TODO(tilal6991): check that dur, ts and merge_col are all present in
  // the tables and have the same type.
  // std::string merge_col = argv[2];

  auto t1_remove_it =
      std::remove_if(t1_.cols.begin(), t1_.cols.end(), [](const Column& it) {
        return it.name == "ts" || it.name == "dur" || it.name == "cpu";
      });
  t1_.cols.erase(t1_remove_it, t1_.cols.end());
  auto t2_remove_it =
      std::remove_if(t2_.cols.begin(), t2_.cols.end(), [](const Column& it) {
        return it.name == "ts" || it.name == "dur" || it.name == "cpu";
      });
  t2_.cols.erase(t2_remove_it, t2_.cols.end());

  std::stringstream ss;
  ss << "CREATE TABLE x("
        "ts UNSIGNED BIG INT, "
        "dur UNSIGNED BIG INT, "
        "cpu UNSIGNED INT, ";
  for (const auto& col : t1_.cols) {
    ss << col.name << " " << col.type << ", ";
  }
  for (const auto& col : t2_.cols) {
    ss << col.name << " " << col.type << ", ";
  }
  ss << "PRIMARY KEY(ts, cpu)"
        ") WITHOUT ROWID;";

  return ss.str();
}

std::unique_ptr<Table::Cursor> SpanTable::CreateCursor() {
  return std::unique_ptr<SpanTable::Cursor>(new SpanTable::Cursor(this, db_));
}

int SpanTable::BestIndex(const QueryConstraints&, BestIndexInfo*) {
  return SQLITE_OK;
}

SpanTable::Cursor::Cursor(SpanTable* table, sqlite3* db)
    : db_(db), table_(table) {}

SpanTable::Cursor::~Cursor() {}

int SpanTable::Cursor::Filter(const QueryConstraints&, sqlite3_value**) {
  std::stringstream ss_t1;
  ss_t1 << "SELECT ts, dur, cpu";
  for (const auto& col : table_->t1_.cols) {
    ss_t1 << ", " << col.name;
  }
  ss_t1 << " FROM " << table_->t1_.name << ";";

  std::string t1_sql = ss_t1.str();
  int t1_size = static_cast<int>(t1_sql.size());

  sqlite3_stmt* t1_stmt;
  int err = sqlite3_prepare_v2(db_, t1_sql.c_str(), t1_size, &t1_stmt, nullptr);
  if (err != SQLITE_OK)
    return err;

  // TODO(tilal6991): remove ts > 0 hack when we figure out why we're seeing
  // those.
  std::stringstream ss_t2;
  ss_t2 << "SELECT ts, dur, ref as cpu";
  for (const auto& col : table_->t2_.cols) {
    ss_t2 << ", " << col.name;
  }
  ss_t2 << " FROM " << table_->t2_.name << " WHERE ts > 0 ORDER BY ts;";

  std::string t2_sql = ss_t2.str();
  int t2_size = static_cast<int>(t2_sql.size());

  sqlite3_stmt* t2_stmt;
  err = sqlite3_prepare_v2(db_, t2_sql.c_str(), t2_size, &t2_stmt, nullptr);
  if (err != SQLITE_OK)
    return err;

  filter_state_.reset(new FilterState(table_, t1_stmt, t2_stmt));

  err = filter_state_->Initialize();
  if (err != SQLITE_OK)
    return err;
  return filter_state_->Next();
}

int SpanTable::Cursor::Next() {
  return filter_state_->Next();
}

int SpanTable::Cursor::Eof() {
  return filter_state_->Eof();
}

int SpanTable::Cursor::Column(sqlite3_context* context, int N) {
  return filter_state_->Column(context, N);
}

SpanTable::FilterState::FilterState(SpanTable* table,
                                    sqlite3_stmt* t1_stmt,
                                    sqlite3_stmt* t2_stmt)
    : t1_stmt_(t1_stmt), t2_stmt_(t2_stmt), table_(table) {}

int SpanTable::FilterState::Initialize() {
  int err = sqlite3_step(t1_stmt_.get());
  if (err != SQLITE_DONE) {
    if (err != SQLITE_ROW)
      return SQLITE_ERROR;
    int64_t ts = sqlite3_column_int64(t1_stmt_.get(), 0);
    latest_t1_ts_ = static_cast<uint64_t>(ts);
  }

  err = sqlite3_step(t2_stmt_.get());
  if (err != SQLITE_DONE) {
    if (err != SQLITE_ROW)
      return SQLITE_ERROR;
    int64_t ts = sqlite3_column_int64(t2_stmt_.get(), 0);
    latest_t2_ts_ = static_cast<uint64_t>(ts);
  }
  return SQLITE_OK;
}

int SpanTable::FilterState::Next() {
  constexpr uint64_t max = std::numeric_limits<uint64_t>::max();
  while (latest_t1_ts_ < max || latest_t2_ts_ < max) {
    int err = ExtractNext(latest_t1_ts_ <= latest_t2_ts_);
    if (err == SQLITE_ROW) {
      is_eof_ = false;
      return SQLITE_OK;
    } else if (err != SQLITE_DONE) {
      return err;
    }
  }
  is_eof_ = true;
  return SQLITE_OK;
}

int SpanTable::FilterState::ExtractNext(bool pull_t1) {
  sqlite3_stmt* stmt = pull_t1 ? t1_stmt_.get() : t2_stmt_.get();

  int64_t ts = sqlite3_column_int64(stmt, 0);
  int64_t dur = sqlite3_column_int64(stmt, 1);
  int32_t cpu = sqlite3_column_int(stmt, 2);

  auto* prev_t1 = &t1_[static_cast<size_t>(cpu)];
  auto* prev_t2 = &t2_[static_cast<size_t>(cpu)];
  auto* prev_pull = pull_t1 ? prev_t1 : prev_t2;
  auto* prev_other = pull_t1 ? prev_t2 : prev_t1;

  TableRow prev = *prev_pull;
  prev_pull->ts = static_cast<uint64_t>(ts);
  prev_pull->dur = static_cast<uint64_t>(dur);

  int col_count = sqlite3_column_count(stmt);
  prev_pull->values.resize(static_cast<size_t>(col_count - kReservedColumns));

  const auto& table_desc = pull_t1 ? table_->t1_ : table_->t2_;
  for (int i = kReservedColumns; i < col_count; i++) {
    size_t off = static_cast<size_t>(i - kReservedColumns);
    std::string type = table_desc.cols[off].type;
    Value* value = &prev_pull->values[off];
    if (type == "UNSIGNED BIG INT") {
      value->type = Value::Type::kULong;
      value->ulong_value = static_cast<uint64_t>(sqlite3_column_int64(stmt, i));
    } else if (type == "UNSIGNED INT") {
      value->type = Value::Type::kUInt;
      value->uint_value = static_cast<uint32_t>(sqlite3_column_int(stmt, i));
    } else if (type == "TEXT") {
      value->type = Value::Type::kText;
      value->text_value =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
    }
  }

  int err = sqlite3_step(stmt);
  if (err == SQLITE_DONE) {
    constexpr uint64_t max = std::numeric_limits<uint64_t>::max();
    if (pull_t1) {
      latest_t1_ts_ = max;
    } else {
      latest_t2_ts_ = max;
    }
  } else if (err != SQLITE_ROW) {
    return SQLITE_ERROR;
  } else {
    ts = sqlite3_column_int64(stmt, 0);
    if (pull_t1) {
      latest_t1_ts_ = static_cast<uint64_t>(ts);
    } else {
      latest_t2_ts_ = static_cast<uint64_t>(ts);
    }
  }

  if (prev_other->ts != 0 && prev.ts != 0) {
    uint64_t other_start = prev_other->ts;
    uint64_t other_end = prev_other->ts + prev_other->dur;

    uint64_t prev_start = prev.ts;
    uint64_t prev_end = prev.ts + prev.dur;

    if (prev_end >= other_start && other_end >= prev_start) {
      // If the start of the other table is before the previous event in
      // this table, we need to account for the time between the start of
      // the previous event and now and vice versa.
      uint64_t span_end = std::min(prev_end, other_end);
      ts_ = std::max(other_start, prev_start);
      dur_ = span_end - ts_;

      cpu_ = static_cast<uint32_t>(cpu);
      t1_to_ret_ = pull_t1 ? prev : *prev_other;
      t2_to_ret_ = pull_t1 ? *prev_other : prev;

      return SQLITE_ROW;
    }
  }
  return SQLITE_DONE;
}

int SpanTable::FilterState::Eof() {
  return is_eof_;
}

int SpanTable::FilterState::Column(sqlite3_context* context, int N) {
  switch (N) {
    case 0:
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(ts_));
      break;
    case 1:
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(dur_));
      break;
    case 2:
      sqlite3_result_int(context, static_cast<int>(cpu_));
      break;
    default: {
      size_t table_1_col = static_cast<size_t>(N - kReservedColumns);
      if (table_1_col < table_->t1_.cols.size()) {
        ReportSqliteResult(context, t1_to_ret_.values[table_1_col]);
      } else {
        size_t table_2_col = table_1_col - table_->t1_.cols.size();
        PERFETTO_CHECK(table_2_col < table_->t2_.cols.size());
        ReportSqliteResult(context, t2_to_ret_.values[table_2_col]);
      }
    }
  }
  return SQLITE_OK;
}

void SpanTable::FilterState::ReportSqliteResult(sqlite3_context* context,
                                                SpanTable::Value value) {
  switch (value.type) {
    case Value::Type::kUInt:
      sqlite3_result_int(context, static_cast<int>(value.uint_value));
      break;
    case Value::Type::kULong:
      sqlite3_result_int64(context,
                           static_cast<sqlite3_int64>(value.ulong_value));
      break;
    case Value::Type::kText:
      sqlite3_result_text(context, value.text_value.c_str(), -1,
                          reinterpret_cast<sqlite3_destructor_type>(-1));
      break;
  }
}

}  // namespace trace_processor
}  // namespace perfetto
