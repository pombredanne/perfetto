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

#include "src/trace_processor/all_events_table.h"

#include "perfetto/base/logging.h"
#include "src/trace_processor/all_events_args_table.h"
#include "src/trace_processor/filtered_row_index.h"
#include "src/trace_processor/query_constraints.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

using namespace sqlite_utils;

}  // namespace

AllEventsTable::AllEventsTable(sqlite3* db, const TraceStorage* storage)
    : storage_(storage) {
  auto fn = [](sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    auto* thiz = static_cast<AllEventsTable*>(sqlite3_user_data(ctx));
    thiz->ToSystrace(ctx, argc, argv);
  };
  sqlite3_create_function(db, "systrace", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                          this, fn, nullptr, nullptr);
}

void AllEventsTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<AllEventsTable>(db, storage, "all_events");
}

base::Optional<Table::Schema> AllEventsTable::Init(int, const char* const*) {
  return Schema(
      {
          Table::Column(Column::kId, "id", ColumnType::kLong),
          Table::Column(Column::kTs, "ts", ColumnType::kLong),
          Table::Column(Column::kName, "name", ColumnType::kString),
          Table::Column(Column::kCpu, "cpu", ColumnType::kUint),
          Table::Column(Column::kUtid, "utid", ColumnType::kUint),
      },
      {Column::kId});
}

std::unique_ptr<Table::Cursor> AllEventsTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  return std::unique_ptr<Table::Cursor>(new Cursor(storage_, qc, argv));
}

int AllEventsTable::BestIndex(const QueryConstraints& qc, BestIndexInfo* info) {
  if (qc.HasConstraint(Column::kId, SQLITE_INDEX_CONSTRAINT_EQ)) {
    info->estimated_cost = 1;
  } else if (qc.HasOrderByAsc(Column::kTs) && qc.constraints().empty()) {
    info->estimated_cost = 100;
  } else {
    info->estimated_cost = storage_->raw_events().raw_event_count() +
                           storage_->slices().slice_count();
  }
  return SQLITE_OK;
}

AllEventsTable::Cursor::Cursor(const TraceStorage* storage,
                               const QueryConstraints& qc,
                               sqlite3_value** argv)
    : storage_(storage) {
  auto opt_idx = qc.HasConstraint(Column::kId, SQLITE_INDEX_CONSTRAINT_EQ);
  if (opt_idx) {
    auto id = sqlite_utils::ExtractSqliteValue<int64_t>(argv[opt_idx.value()]);
    auto parsed = TraceStorage::ParseRowId(id);
    // TODO(lalitm): bounds check the row.
    if (parsed.first == TableId::kRawEvents) {
      FilteredRowIndex raw_index(parsed.second, parsed.second + 1);
      FilteredRowIndex sched_index(0, 0);
      raw_it_ = raw_index.ToRowIterator(false);
      sched_it_ = sched_index.ToRowIterator(false);
    } else if (parsed.first == TableId::kSched) {
      FilteredRowIndex raw_index(0, 0);
      FilteredRowIndex sched_index(parsed.second, parsed.second + 1);
      raw_it_ = raw_index.ToRowIterator(false);
      sched_it_ = sched_index.ToRowIterator(false);
    } else {
      FilteredRowIndex raw_index(0, 0);
      FilteredRowIndex sched_index(0, 0);
      raw_it_ = raw_index.ToRowIterator(false);
      sched_it_ = sched_index.ToRowIterator(false);
    }
  } else {
    FilteredRowIndex raw_index(0, storage_->raw_events().raw_event_count());
    FilteredRowIndex sched_index(0, storage_->slices().slice_count() - 1);
    raw_it_ = raw_index.ToRowIterator(false);
    sched_it_ = sched_index.ToRowIterator(false);
  }

  if (!raw_it_->IsEnd() && !sched_it_->IsEnd()) {
    auto raw_ts = storage_->raw_events().timestamps()[raw_it_->Row()];
    auto sched_ts = storage_->slices().start_ns()[sched_it_->Row()];
    type_ = raw_ts < sched_ts ? Type::kRaw : Type::kSched;
  } else if (!sched_it_->IsEnd()) {
    type_ = Type::kSched;
  } else if (!raw_it_->IsEnd()) {
    type_ = Type::kRaw;
  }
}

int AllEventsTable::Cursor::Column(sqlite3_context* ctx, int N) {
  const auto& raw = storage_->raw_events();
  const auto& sched = storage_->slices();

  auto raw_row = raw_it_->Row();
  auto sched_row = sched_it_->Row();

  switch (N) {
    case Column::kId: {
      if (type_ == Type::kRaw) {
        sqlite_utils::ReportSqliteResult(
            ctx, TraceStorage::CreateRowId(TableId::kRawEvents, raw_row));
      } else /* (type == kSched) */ {
        sqlite_utils::ReportSqliteResult(
            ctx, TraceStorage::CreateRowId(TableId::kSched, sched_row));
      }
      break;
    }
    case Column::kTs: {
      if (type_ == Type::kRaw) {
        sqlite_utils::ReportSqliteResult(ctx, raw.timestamps()[raw_row]);
      } else /* (type == kSched) */ {
        sqlite_utils::ReportSqliteResult(ctx, sched.start_ns()[sched_row]);
      }
      break;
    }
    case Column::kName: {
      if (type_ == Type::kRaw) {
        sqlite_utils::ReportSqliteResult(
            ctx, storage_->GetString(raw.name_ids()[raw_row]).c_str());
      } else /* (type == kSched) */ {
        sqlite_utils::ReportSqliteResult(ctx, "sched_switch");
      }
      break;
    }
    case Column::kCpu: {
      if (type_ == Type::kRaw) {
        sqlite_utils::ReportSqliteResult(ctx, raw.cpus()[raw_row]);
      } else /* (type == kSched) */ {
        sqlite_utils::ReportSqliteResult(ctx, sched.cpus()[sched_row]);
      }
      break;
    }
    case Column::kUtid: {
      if (type_ == Type::kRaw) {
        sqlite_utils::ReportSqliteResult(ctx, raw.utids()[raw_row]);
      } else /* (type == kSched) */ {
        sqlite_utils::ReportSqliteResult(ctx, sched.utids()[sched_row]);
      }
      break;
    }
    default:
      PERFETTO_FATAL("Unknown column %d", N);
      break;
  }
  return SQLITE_OK;
}

int AllEventsTable::Cursor::Next() {
  if (type_ == Type::kRaw) {
    raw_it_->NextRow();
  } else /* (type == kSched) */ {
    sched_it_->NextRow();
  }

  if (!raw_it_->IsEnd() && !sched_it_->IsEnd()) {
    auto raw_ts = storage_->raw_events().timestamps()[raw_it_->Row()];
    auto sched_ts = storage_->slices().start_ns()[sched_it_->Row()];
    type_ = raw_ts < sched_ts ? Type::kRaw : Type::kSched;
  } else if (!sched_it_->IsEnd()) {
    type_ = Type::kSched;
  } else if (!raw_it_->IsEnd()) {
    type_ = Type::kRaw;
  }
  return SQLITE_OK;
}

int AllEventsTable::Cursor::Eof() {
  return raw_it_->IsEnd() && sched_it_->IsEnd();
}

int AllEventsTable::FormatSystraceArgs(TableId table_id,
                                       uint32_t row,
                                       char* line,
                                       size_t size) {
  const auto& args = storage_->args();
  const auto& sched = storage_->slices();

  switch (table_id) {
    case TableId::kRawEvents: {
      const auto& row_and_count = args.raw_table_args_for_id_2()[row];
      int n = 0;
      for (size_t i = 0; i < row_and_count.count; i++) {
        auto arg_row = row_and_count.start + i;
        if (i != 0)
          n += snprintf(line + n, size - static_cast<size_t>(n), " ");
        n += snprintf(line + n, size - static_cast<size_t>(n),
                      "%s=", storage_->GetString(args.keys()[arg_row]).c_str());
        const auto& value = args.arg_values()[arg_row];
        switch (value.type) {
          case TraceStorage::Args::Variadic::kInt:
            n += snprintf(line + n, size - static_cast<size_t>(n), "%lld",
                          value.int_value);
            break;
          case TraceStorage::Args::Variadic::kReal:
            n += snprintf(line + n, size - static_cast<size_t>(n), "%f",
                          value.real_value);
            break;
          case TraceStorage::Args::Variadic::kString:
            n += snprintf(line + n, size - static_cast<size_t>(n), "%s",
                          storage_->GetString(value.string_value).c_str());
            break;
        }
      }
      return n;
    }
    case TableId::kSched: {
      const auto& prev_thread = storage_->GetThread(sched.utids()[row]);
      const auto& next_thread = storage_->GetThread(sched.utids()[row + 1]);
      const auto& state = sched.end_state()[row];
      return snprintf(
          line, size,
          "prev_comm=%s "
          "prev_pid=%d prev_prio=%d prev_state=%s ==> next_comm=%s next_pid=%d "
          "next_prio=%d",
          storage_->GetString(prev_thread.name_id).c_str(),
          storage_->GetThread(sched.utids()[row]).tid, sched.priorities()[row],
          state.is_valid() ? state.ToString().data() : "?",
          storage_->GetString(next_thread.name_id).c_str(),
          storage_->GetThread(sched.utids()[row + 1]).tid,
          sched.priorities()[row + 1]);
    }
    case TableId::kCounters:
    case TableId::kInstants:
      PERFETTO_CHECK(false);
  }
  PERFETTO_CHECK(false);
}

void AllEventsTable::ToSystrace(sqlite3_context* ctx,
                                int argc,
                                sqlite3_value** argv) {
  if (argc != 1 || sqlite3_value_type(argv[0]) != SQLITE_INTEGER) {
    sqlite3_result_error(ctx, "Usage: systrace(id)", -1);
    return;
  }
  RowId id = sqlite3_value_int64(argv[0]);
  auto pair = TraceStorage::ParseRowId(id);
  auto table_id = static_cast<TableId>(pair.first);
  auto row = pair.second;

  const auto& sched = storage_->slices();
  if (table_id == TableId::kSched && row == sched.slice_count() - 1) {
    sqlite3_result_null(ctx);
    return;
  }

  UniqueTid utid = Utid(table_id, row);
  const auto& thread = storage_->GetThread(utid);
  uint32_t tgid = 0;
  if (thread.upid.has_value()) {
    tgid = storage_->GetProcess(thread.upid.value()).pid;
  }
  const auto& name = storage_->GetString(thread.name_id);

  char line[2048];
  int n = 0;
  n += ftrace_utils::FormatSystracePrefix(
      Timestamp(table_id, row), Cpu(table_id, row), thread.tid, tgid,
      base::StringView(name), line, sizeof(line) - static_cast<size_t>(n));
  PERFETTO_CHECK(static_cast<size_t>(n) < sizeof(line));

  if (table_id == TableId::kSched) {
    n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n),
                  "%s: ", "sched_switch");
  } else {
    n += snprintf(
        line + n, sizeof(line) - static_cast<size_t>(n), "%s: ",
        storage_->GetString(storage_->raw_events().name_ids()[row]).c_str());
  }
  FormatSystraceArgs(table_id, row, line + n,
                     sizeof(line) - static_cast<size_t>(n));
  sqlite3_result_text(ctx, line, -1, sqlite_utils::kSqliteTransient);
}

}  // namespace trace_processor
}  // namespace perfetto
