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

#include "src/trace_processor/all_events_args_table.h"

#include "perfetto/base/logging.h"
#include "src/trace_processor/query_constraints.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

using namespace sqlite_utils;

}  // namespace

AllEventsArgsTable::AllEventsArgsTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void AllEventsArgsTable::RegisterTable(sqlite3* db,
                                       const TraceStorage* storage) {
  Table::Register<AllEventsArgsTable>(db, storage, "all_events_args");
}

base::Optional<Table::Schema> AllEventsArgsTable::Init(int,
                                                       const char* const*) {
  return Schema(
      {
          Table::Column(Column::kRowId, "id", ColumnType::kLong),
          Table::Column(Column::kFlatKey, "flat_key", ColumnType::kString),
          Table::Column(Column::kKey, "key", ColumnType::kString),
          Table::Column(Column::kIntValue, "int_value", ColumnType::kLong),
          Table::Column(Column::kStringValue, "string_value",
                        ColumnType::kString),
          Table::Column(Column::kRealValue, "real_value", ColumnType::kDouble),
      },
      {Column::kRowId, Column::kKey});
}

std::unique_ptr<Table::Cursor> AllEventsArgsTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  return std::unique_ptr<Table::Cursor>(new Cursor(storage_, qc, argv));
}

int AllEventsArgsTable::BestIndex(const QueryConstraints& qc,
                                  BestIndexInfo* info) {
  if (qc.HasConstraint(Column::kRowId, SQLITE_INDEX_CONSTRAINT_EQ)) {
    info->estimated_cost = 1;
  } else if (qc.constraints().empty() && qc.order_by().empty()) {
    info->estimated_cost = 10000;
  } else {
    info->estimated_cost =
        storage_->args().args_count() + storage_->slices().slice_count();
  }
  return SQLITE_OK;
}

AllEventsArgsTable::Cursor::Cursor(const TraceStorage* storage,
                                   const QueryConstraints& qc,
                                   sqlite3_value** argv)
    : storage_(storage) {
  auto opt_idx = qc.HasConstraint(Column::kRowId, SQLITE_INDEX_CONSTRAINT_EQ);
  if (opt_idx) {
    auto id = sqlite_utils::ExtractSqliteValue<int64_t>(argv[opt_idx.value()]);
    auto parsed = TraceStorage::ParseRowId(id);
    // TODO(lalitm): bounds check the row.
    if (parsed.first == TableId::kRawEvents) {
      // TODO(lalitm): add support for this case using prefix sums.
      FilteredRowIndex args_index(0, storage_->args().args_count());
      FilteredRowIndex sched_index(0, 0);
      args_it_ = args_index.ToRowIterator(false);
      sched_it_ = sched_index.ToRowIterator(false);
    } else if (parsed.first == TableId::kSched) {
      FilteredRowIndex args_index(0, 0);
      FilteredRowIndex sched_index(parsed.second * 7, (parsed.second + 1) * 7);
      args_it_ = args_index.ToRowIterator(false);
      sched_it_ = sched_index.ToRowIterator(false);
    } else {
      FilteredRowIndex args_index(0, 0);
      FilteredRowIndex sched_index(0, 0);
      args_it_ = args_index.ToRowIterator(false);
      sched_it_ = sched_index.ToRowIterator(false);
    }
  } else {
    FilteredRowIndex args_index(0, storage_->args().args_count());
    FilteredRowIndex sched_index(0, (storage_->slices().slice_count() - 1) * 7);
    args_it_ = args_index.ToRowIterator(false);
    sched_it_ = sched_index.ToRowIterator(false);
  }

  if (!args_it_->IsEnd() && !sched_it_->IsEnd()) {
    // Don't switch iteration type.
  } else if (!sched_it_->IsEnd()) {
    type_ = Type::kSched;
  } else if (!args_it_->IsEnd()) {
    type_ = Type::kArgs;
  }
}

int AllEventsArgsTable::Cursor::Column(sqlite3_context* ctx, int N) {
  const auto& args = storage_->args();
  const auto& sched = storage_->slices();
  switch (N) {
    case Column::kRowId: {
      if (type_ == Type::kArgs) {
        sqlite_utils::ReportSqliteResult(ctx, args.ids()[args_it_->Row()]);
      } else /* (type == kSched) */ {
        auto sched_row = sched_it_->Row() / 7;
        sqlite_utils::ReportSqliteResult(
            ctx, TraceStorage::CreateRowId(TableId::kSched, sched_row));
      }
      break;
    }
    case Column::kFlatKey: {
      if (type_ == Type::kArgs) {
        sqlite_utils::ReportSqliteResult(
            ctx,
            storage_->GetString(args.flat_keys()[args_it_->Row()]).c_str());
      } else /* (type == kSched) */ {
        auto sched_field = sched_it_->Row() % 7;
        switch (sched_field) {
          case kPrevComm:
            sqlite_utils::ReportSqliteResult(ctx, "prev_comm");
            break;
          case kPrevPid:
            sqlite_utils::ReportSqliteResult(ctx, "prev_pid");
            break;
          case kPrevPrio:
            sqlite_utils::ReportSqliteResult(ctx, "prev_prio");
            break;
          case kPrevState:
            sqlite_utils::ReportSqliteResult(ctx, "prev_state");
            break;
          case kNextComm:
            sqlite_utils::ReportSqliteResult(ctx, "next_comm");
            break;
          case kNextPid:
            sqlite_utils::ReportSqliteResult(ctx, "next_pid");
            break;
          case kNextPrio:
            sqlite_utils::ReportSqliteResult(ctx, "next_prio");
            break;
          case kMax:
            PERFETTO_FATAL("Invalid sched field");
        }
      }
      break;
    }
    case Column::kKey: {
      if (type_ == Type::kArgs) {
        sqlite_utils::ReportSqliteResult(
            ctx, storage_->GetString(args.keys()[args_it_->Row()]).c_str());
      } else /* (type == kSched) */ {
        auto sched_field = sched_it_->Row() % 7;
        switch (sched_field) {
          case kPrevComm:
            sqlite_utils::ReportSqliteResult(ctx, "prev_comm");
            break;
          case kPrevPid:
            sqlite_utils::ReportSqliteResult(ctx, "prev_pid");
            break;
          case kPrevPrio:
            sqlite_utils::ReportSqliteResult(ctx, "prev_prio");
            break;
          case kPrevState:
            sqlite_utils::ReportSqliteResult(ctx, "prev_state");
            break;
          case kNextComm:
            sqlite_utils::ReportSqliteResult(ctx, "next_comm");
            break;
          case kNextPid:
            sqlite_utils::ReportSqliteResult(ctx, "next_pid");
            break;
          case kNextPrio:
            sqlite_utils::ReportSqliteResult(ctx, "next_prio");
            break;
          case kMax:
            PERFETTO_FATAL("Invalid sched field");
        }
      }
      break;
    }
    case Column::kIntValue: {
      if (type_ == Type::kArgs) {
        auto value = args.arg_values()[args_it_->Row()];
        if (value.type == TraceStorage::Args::Variadic::kInt) {
          sqlite_utils::ReportSqliteResult(ctx, value.int_value);
        } else {
          sqlite3_result_null(ctx);
        }
      } else /* (type == kSched) */ {
        auto sched_row = sched_it_->Row() / 7;
        auto sched_field = sched_it_->Row() % 7;
        switch (sched_field) {
          case kPrevPid:
            sqlite_utils::ReportSqliteResult(
                ctx, storage_->GetThread(sched.utids()[sched_row]).tid);
            break;
          case kPrevPrio:
            sqlite_utils::ReportSqliteResult(ctx,
                                             sched.priorities()[sched_row]);
            break;
          case kPrevState:
            sqlite_utils::ReportSqliteResult(
                ctx, sched.end_state()[sched_row].raw_state());
            break;
          case kNextPid:
            sqlite_utils::ReportSqliteResult(
                ctx, storage_->GetThread(sched.utids()[sched_row + 1]).tid);
            break;
          case kNextPrio:
            sqlite_utils::ReportSqliteResult(ctx,
                                             sched.priorities()[sched_row + 1]);
            break;
          case kPrevComm:
          case kNextComm:
            sqlite3_result_null(ctx);
            break;
          case kMax:
            PERFETTO_FATAL("Invalid sched field");
        }
      }
      break;
    }
    case Column::kStringValue: {
      if (type_ == Type::kArgs) {
        auto value = args.arg_values()[args_it_->Row()];
        if (value.type == TraceStorage::Args::Variadic::kString) {
          sqlite_utils::ReportSqliteResult(
              ctx, storage_->GetString(value.string_value).c_str());
        } else {
          sqlite3_result_null(ctx);
        }
      } else /* (type == kSched) */ {
        auto sched_row = sched_it_->Row() / 7;
        auto sched_field = sched_it_->Row() % 7;
        switch (sched_field) {
          case kPrevComm: {
            const auto& thread = storage_->GetThread(sched.utids()[sched_row]);
            sqlite_utils::ReportSqliteResult(
                ctx, storage_->GetString(thread.name_id).c_str());
            break;
          }
          case kNextComm: {
            const auto& thread =
                storage_->GetThread(sched.utids()[sched_row + 1]);
            sqlite_utils::ReportSqliteResult(
                ctx, storage_->GetString(thread.name_id).c_str());
            break;
          }
          case kPrevPid:
          case kPrevPrio:
          case kPrevState:
          case kNextPid:
          case kNextPrio:
            sqlite3_result_null(ctx);
            break;
          case kMax:
            PERFETTO_FATAL("Invalid sched field");
        }
      }
      break;
    }
    case Column::kRealValue: {
      if (type_ == Type::kArgs) {
        auto value = args.arg_values()[args_it_->Row()];
        if (value.type == TraceStorage::Args::Variadic::kReal) {
          sqlite_utils::ReportSqliteResult(ctx, value.real_value);
        } else {
          sqlite3_result_null(ctx);
        }
      } else /* (type == kSched) */ {
        sqlite3_result_null(ctx);
      }
      break;
    }
    default:
      PERFETTO_FATAL("Unknown column %d", N);
      break;
  }
  return SQLITE_OK;
}

int AllEventsArgsTable::Cursor::Next() {
  if (type_ == Type::kArgs) {
    args_it_->NextRow();
  } else /* (type == kSched) */ {
    sched_it_->NextRow();
  }

  if (!args_it_->IsEnd() && !sched_it_->IsEnd()) {
    // Don't switch iteration type.
  } else if (!sched_it_->IsEnd()) {
    type_ = Type::kSched;
  } else if (!args_it_->IsEnd()) {
    type_ = Type::kArgs;
  }
  return SQLITE_OK;
}

int AllEventsArgsTable::Cursor::Eof() {
  return args_it_->IsEnd() && sched_it_->IsEnd();
}

}  // namespace trace_processor
}  // namespace perfetto
