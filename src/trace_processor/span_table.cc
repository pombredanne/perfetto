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
  while (!err) {
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

  t1_.cols = GetColumnsForTable(db_, argv[3]);
  t2_.cols = GetColumnsForTable(db_, argv[4]);

  // TODO(tilal6991): check that dur, ts and merge_col are all present in
  // the tables and have the same type.
  // std::string merge_col = argv[2];

  t1_.cols.erase(
      std::remove_if(t1_.cols.begin(), t1_.cols.end(), [](const Column& it) {
        return it.name == "ts" || it.name == "dur" || it.name == "cpu";
      }));
  t2_.cols.erase(
      std::remove_if(t2_.cols.begin(), t2_.cols.end(), [](const Column& it) {
        return it.name == "ts" || it.name == "dur" || it.name == "cpu";
      }));

  std::stringstream ss;
  ss << "CREATE TABLE x("
        "ts UNSIGNED BIG INT, "
        "dur UNSIGNED BIG INT, "
        "cpu UNSIGNED INT, ";
  for (const auto& col : t1_.cols) {
    ss << col.name << " " << col.type;
    ss << ", ";
  }
  for (const auto& col : t2_.cols) {
    ss << col.name << " " << col.type;
    ss << ", ";
  }
  ss << "PRIMARY KEY(ts, cpu)"
        ") WITHOUT ROWID;";

  return ss.str();
}

std::unique_ptr<Table::Cursor> SpanTable::CreateCursor() {
  return std::unique_ptr<SpanTable::Cursor>(new SpanTable::Cursor(db_));
}

int SpanTable::BestIndex(const QueryConstraints&, BestIndexInfo*) {
  return SQLITE_OK;
}

SpanTable::Cursor::Cursor(sqlite3* db) : db_(db) {}

SpanTable::Cursor::~Cursor() {}

int SpanTable::Cursor::Filter(const QueryConstraints&, sqlite3_value**) {
  const char kRawSqlT1[] = "SELECT ts, dur, cpu, utid from sched ORDER BY ts;";
  sqlite3_stmt* t1_stmt;
  int err = sqlite3_prepare_v2(db_, kRawSqlT1, sizeof(kRawSqlT1) - 1, &t1_stmt,
                               nullptr);
  if (err != SQLITE_OK || sqlite3_column_count(t1_stmt) != 4)
    return err == SQLITE_OK ? SQLITE_ERROR : err;

  // TODO(tilal6991): remove ts > 0 hack when we figure out why we're seeing
  // those.
  const char kRawSqlT2[] =
      "SELECT ts, dur, ref as cpu, value as freq "
      "from counters "
      "WHERE ts > 0 "
      "ORDER BY ts;";
  sqlite3_stmt* t2_stmt;
  err = sqlite3_prepare_v2(db_, kRawSqlT2, sizeof(kRawSqlT2) - 1, &t2_stmt,
                           nullptr);
  if (err != SQLITE_OK || sqlite3_column_count(t2_stmt) != 4)
    return err == SQLITE_OK ? SQLITE_ERROR : err;

  filter_state_.reset(new FilterState(t1_stmt, t2_stmt));
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

SpanTable::FilterState::FilterState(sqlite3_stmt* t1_stmt,
                                    sqlite3_stmt* t2_stmt)
    : t1_stmt_(t1_stmt), t2_stmt_(t2_stmt) {}

int SpanTable::FilterState::Next() {
  if (PERFETTO_UNLIKELY(latest_t1_ts_ == 0)) {
    int err = sqlite3_step(t1_stmt_.get());
    if (err == SQLITE_DONE)
      return SQLITE_OK;
    if (err != SQLITE_ROW)
      return SQLITE_ERROR;
    int64_t ts = sqlite3_column_int64(t1_stmt_.get(), 0);
    latest_t1_ts_ = static_cast<uint64_t>(ts);

    err = sqlite3_step(t2_stmt_.get());
    if (err == SQLITE_DONE)
      return SQLITE_OK;
    if (err != SQLITE_ROW)
      return SQLITE_ERROR;
    ts = sqlite3_column_int64(t2_stmt_.get(), 0);
    latest_t2_ts_ = static_cast<uint64_t>(ts);
  }

  while (true) {
    if (latest_t1_ts_ <= latest_t2_ts_) {
      int64_t ts = sqlite3_column_int64(t1_stmt_.get(), 0);
      int64_t dur = sqlite3_column_int64(t1_stmt_.get(), 1);
      int32_t cpu = sqlite3_column_int(t1_stmt_.get(), 2);

      latest_t1_ts_ = static_cast<uint64_t>(ts);

      auto* prev_sched = &sched_[static_cast<size_t>(cpu)];
      auto* prev_freq = &freq_[static_cast<size_t>(cpu)];

      Sched prev = *prev_sched;
      prev_sched->ts = static_cast<uint64_t>(ts);
      prev_sched->dur = static_cast<uint64_t>(dur);
      prev_sched->utid =
          static_cast<uint64_t>(sqlite3_column_int64(t1_stmt_.get(), 3));

      int err = sqlite3_step(t1_stmt_.get());
      if (err == SQLITE_DONE)
        return SQLITE_OK;
      if (err != SQLITE_ROW)
        return SQLITE_ERROR;
      ts = sqlite3_column_int64(t1_stmt_.get(), 0);
      latest_t1_ts_ = static_cast<uint64_t>(ts);

      if (prev_freq->ts != 0 && prev.ts != 0) {
        uint64_t other_start = prev_freq->ts;
        uint64_t other_end = prev_freq->ts + prev_freq->dur;

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
          sched_to_ret_ = prev;
          freq_to_ret_ = *prev_freq;

          return SQLITE_OK;
        }
      }
    } else {
      int64_t ts = sqlite3_column_int64(t2_stmt_.get(), 0);
      int64_t dur = sqlite3_column_int64(t2_stmt_.get(), 1);
      int32_t cpu = sqlite3_column_int(t2_stmt_.get(), 2);

      latest_t2_ts_ = static_cast<uint64_t>(ts);

      auto* prev_sched = &sched_[static_cast<size_t>(cpu)];
      auto* prev_freq = &freq_[static_cast<size_t>(cpu)];

      Freq prev = *prev_freq;
      prev_freq->ts = static_cast<uint64_t>(ts);
      prev_freq->dur = static_cast<uint64_t>(dur);
      prev_freq->freq =
          static_cast<uint64_t>(sqlite3_column_int64(t2_stmt_.get(), 3));

      int err = sqlite3_step(t2_stmt_.get());
      if (err == SQLITE_DONE)
        return SQLITE_OK;
      if (err != SQLITE_ROW)
        return SQLITE_ERROR;
      ts = sqlite3_column_int64(t2_stmt_.get(), 0);
      latest_t2_ts_ = static_cast<uint64_t>(ts);

      if (prev_sched->ts != 0 && prev.ts != 0) {
        uint64_t other_start = prev_sched->ts;
        uint64_t other_end = prev_sched->ts + prev_sched->dur;

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
          sched_to_ret_ = *prev_sched;
          freq_to_ret_ = prev;
          return SQLITE_OK;
        }
      }
    }
  }
  return SQLITE_OK;
}

int SpanTable::FilterState::Eof() {
  return i_++ >= 100;
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
    case 3:
      sqlite3_result_int64(context,
                           static_cast<sqlite3_int64>(sched_to_ret_.utid));
      break;
    case 4:
      sqlite3_result_int64(context,
                           static_cast<sqlite3_int64>(freq_to_ret_.freq));
      break;
  }
  return SQLITE_OK;
}

}  // namespace trace_processor
}  // namespace perfetto
