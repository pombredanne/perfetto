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

#include "src/trace_processor/counters_table.h"

#include <bitset>
#include <numeric>

#include "perfetto/base/logging.h"
#include "src/trace_processor/query_constraints.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

using namespace sqlite_utils;

constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kInt64Max = std::numeric_limits<int64_t>::max();

template <class T>
inline int Compare(T first, T second, bool desc) {
  if (first < second) {
    return desc ? 1 : -1;
  } else if (first > second) {
    return desc ? -1 : 1;
  }
  return 0;
}

}  // namespace

CountersTable::CountersTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void CountersTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<CountersTable>(db, storage, "counters");
}

std::string CountersTable::CreateTableStmt(int, const char* const*) {
  return "CREATE TABLE x("
         "ts UNSIGNED BIG INT, "
         "name text, "
         "value UNSIGNED BIG INT, "
         "dur UNSIGNED BIG INT, "
         "value_delta UNSIGNED BIG INT, "
         "ref UNSIGNED INT, "
         "ref_type TEXT, "
         "PRIMARY KEY(name, ts, ref)"
         ") WITHOUT ROWID;";
}

std::unique_ptr<Table::Cursor> CountersTable::CreateCursor() {
  return std::unique_ptr<Table::Cursor>(new Cursor(storage_));
}

int CountersTable::BestIndex(const QueryConstraints& qc, BestIndexInfo* info) {
  bool is_time_constrained = false;
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    const auto& cs = qc.constraints()[i];
    if (cs.iColumn == Column::kTimestamp)
      is_time_constrained = true;
  }
  info->estimated_cost = is_time_constrained ? 10 : 10000;

  return SQLITE_OK;
}

CountersTable::Cursor::Cursor(const TraceStorage* storage)
    : storage_(storage) {}

int CountersTable::Cursor::Column(sqlite3_context* context, int N) {
  size_t row = filter_state_->next_row_id();
  const auto& counters = storage_->counters();
  switch (N) {
    case Column::kTimestamp: {
      sqlite3_result_int64(context,
                           static_cast<int64_t>(counters.timestamps()[row]));
      break;
    }
    case Column::kValue: {
      sqlite3_result_int64(context,
                           static_cast<int64_t>(counters.values()[row]));
      break;
    }
    case Column::kName: {
      sqlite3_result_text(context,
                          storage_->GetString(counters.name_ids()[row]).c_str(),
                          -1, nullptr);
      break;
    }
    case Column::kRef: {
      sqlite3_result_int64(context, static_cast<int64_t>(counters.refs()[row]));
      break;
    }
    case Column::kRefType: {
      switch (counters.types()[row]) {
        case RefType::kCPU_ID: {
          sqlite3_result_text(context, "cpu", -1, nullptr);
          break;
        }
        case RefType::kUPID: {
          sqlite3_result_text(context, "upid", -1, nullptr);
          break;
        }
      }
      break;
    }
    case Column::kDuration: {
      sqlite3_result_int64(context,
                           static_cast<int64_t>(counters.durations()[row]));
      break;
    }
    case Column::kValueDelta: {
      sqlite3_result_int64(context,
                           static_cast<int64_t>(counters.value_deltas()[row]));
      break;
    }
    default:
      PERFETTO_FATAL("Unknown column %d", N);
      break;
  }
  return SQLITE_OK;
}

CountersTable::FilterState::FilterState(
    const TraceStorage* storage,
    const QueryConstraints& query_constraints,
    sqlite3_value** argv)
    : order_by_(query_constraints.order_by()), storage_(storage) {
  uint64_t min_ts = 0;
  uint64_t max_ts = kUint64Max;
  int64_t min_ref = 0;
  int64_t max_ref = kInt64Max;
  RefType ref_type_filter = RefType::kCPU_ID;
  // Set if we need to filter on that column.
  std::bitset<7> filter_column;  // TODO(taylori): Avoid hardcoding # of cols

  for (size_t i = 0; i < query_constraints.constraints().size(); i++) {
    const auto& cs = query_constraints.constraints()[i];
    switch (cs.iColumn) {
      case Column::kRefType: {
        if (IsOpEq(cs.op)) {
          filter_column.set(Column::kRefType);
          std::string type(
              reinterpret_cast<const char*>(sqlite3_value_text(argv[i])));
          if (type == "cpu") {
            ref_type_filter = RefType::kCPU_ID;
          } else if (type == "upid") {
            ref_type_filter = RefType::kUPID;
          }
        }
        break;
      }
      case Column::kRef: {
        filter_column.set(Column::kRef);
        auto ref = sqlite3_value_int64(argv[i]);
        if (IsOpEq(cs.op)) {
          min_ref = ref;
          max_ref = ref;
        } else if (IsOpGe(cs.op) || IsOpGt(cs.op)) {
          min_ref = IsOpGe(cs.op) ? ref : ref + 1;
        } else if (IsOpLe(cs.op) || IsOpLt(cs.op)) {
          max_ref = IsOpLe(cs.op) ? ref : ref - 1;
        }
        break;
      }
      case Column::kTimestamp: {
        auto ts = static_cast<uint64_t>(sqlite3_value_int64(argv[i]));
        if (IsOpGe(cs.op) || IsOpGt(cs.op)) {
          min_ts = IsOpGe(cs.op) ? ts : ts + 1;
        } else if (IsOpLe(cs.op) || IsOpLt(cs.op)) {
          max_ts = IsOpLe(cs.op) ? ts : ts - 1;
        }
        break;
      }
    }
  }
  SetupSortedRowIds(min_ts, max_ts);

  const auto& counters = storage_->counters();
  row_filter_.resize(sorted_row_ids_.size(), true);
  if (filter_column.test(Column::kRefType)) {
    for (size_t i = 0; i < sorted_row_ids_.size(); i++) {
      row_filter_[i] = ref_type_filter == counters.types()[sorted_row_ids_[i]];
    }
  }
  if (filter_column.test(Column::kRef)) {
    for (size_t i = 0; i < sorted_row_ids_.size(); i++) {
      if (row_filter_[i]) {
        const auto& ref_to_check = counters.refs()[sorted_row_ids_[i]];
        row_filter_[i] = ref_to_check >= min_ref && ref_to_check <= max_ref;
      }
    }
  }
  FindNextRowAndTimestamp();
}

int CountersTable::FilterState::CompareSlicesOnColumn(
    size_t f_idx,
    size_t s_idx,
    const QueryConstraints::OrderBy& ob) {
  const auto& c = storage_->counters();
  switch (ob.iColumn) {
    case CountersTable::Column::kTimestamp:
      return Compare(c.timestamps()[f_idx], c.timestamps()[s_idx], ob.desc);
    case CountersTable::Column::kName:
      return Compare(storage_->GetString(c.name_ids()[f_idx]),
                     storage_->GetString(c.name_ids()[s_idx]), ob.desc);
    case CountersTable::Column::kValue:
      return Compare(c.values()[f_idx], c.values()[s_idx], ob.desc);
    case CountersTable::Column::kDuration:
      return Compare(c.durations()[f_idx], c.durations()[s_idx], ob.desc);
    case CountersTable::Column::kValueDelta:
      return Compare(c.value_deltas()[f_idx], c.value_deltas()[s_idx], ob.desc);
    case CountersTable::Column::kRef:
      return Compare(c.refs()[f_idx], c.refs()[s_idx], ob.desc);
    case CountersTable::Column::kRefType:
      return Compare(c.types()[f_idx], c.types()[s_idx], ob.desc);
  }
  PERFETTO_FATAL("Unexpected column %d", ob.iColumn);
}

void CountersTable::FilterState::SetupSortedRowIds(uint64_t min_ts,
                                                   uint64_t max_ts) {
  const auto& counters = storage_->counters();

  const auto& timestamps = counters.timestamps();
  PERFETTO_CHECK(counters.counter_count() <=
                 std::numeric_limits<uint32_t>::max());

  auto min_it = std::lower_bound(timestamps.begin(), timestamps.end(), min_ts);
  auto max_it = std::upper_bound(min_it, timestamps.end(), max_ts);
  ptrdiff_t dist = std::distance(min_it, max_it);
  PERFETTO_CHECK(dist >= 0 && static_cast<size_t>(dist) <= timestamps.size());

  // Fill |indices| with the consecutive row numbers affected by the filtering.
  sorted_row_ids_.resize(static_cast<size_t>(dist));
  std::iota(sorted_row_ids_.begin(), sorted_row_ids_.end(),
            std::distance(timestamps.begin(), min_it));

  // Sort if there is any order by constraints.
  if (!order_by_.empty()) {
    std::sort(
        sorted_row_ids_.begin(), sorted_row_ids_.end(),
        [this](uint32_t f, uint32_t s) { return CompareSlices(f, s) < 0; });
  }
}

int CountersTable::FilterState::CompareSlices(size_t f_idx, size_t s_idx) {
  for (const auto& ob : order_by_) {
    int c = CompareSlicesOnColumn(f_idx, s_idx, ob);
    if (c != 0)
      return c;
  }
  return 0;
}

void CountersTable::FilterState::FindNextCounter() {
  next_row_id_index_++;
  FindNextRowAndTimestamp();
}

void CountersTable::FilterState::FindNextRowAndTimestamp() {
  auto start =
      row_filter_.begin() +
      static_cast<decltype(row_filter_)::difference_type>(next_row_id_index_);
  auto next_it = std::find(start, row_filter_.end(), true);
  next_row_id_index_ =
      static_cast<uint32_t>(std::distance(row_filter_.begin(), next_it));
}

int CountersTable::Cursor::Filter(const QueryConstraints& qc,
                                  sqlite3_value** argv) {
  filter_state_.reset(new FilterState(storage_, qc, argv));
  return SQLITE_OK;
}

int CountersTable::Cursor::Next() {
  filter_state_->FindNextCounter();
  return SQLITE_OK;
}

int CountersTable::Cursor::Eof() {
  return !filter_state_->IsNextRowIdIndexValid();
}

}  // namespace trace_processor
}  // namespace perfetto
