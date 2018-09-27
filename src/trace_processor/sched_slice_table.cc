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

#include "src/trace_processor/sched_slice_table.h"

#include <string.h>
#include <algorithm>
#include <bitset>
#include <numeric>

#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

using namespace sqlite_utils;

constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();

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

SchedSliceTable::SchedSliceTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void SchedSliceTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<SchedSliceTable>(db, storage, "sched");
}

Table::Schema SchedSliceTable::CreateSchema(int, const char* const*) {
  return Schema(
      {
          Table::Column(Column::kTimestamp, "ts", ColumnType::kUlong),
          Table::Column(Column::kCpu, "cpu", ColumnType::kUint),
          Table::Column(Column::kDuration, "dur", ColumnType::kUlong),
          Table::Column(Column::kUtid, "utid", ColumnType::kUint),
      },
      {Column::kCpu, Column::kTimestamp});
}

std::unique_ptr<Table::Cursor> SchedSliceTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  return std::unique_ptr<Table::Cursor>(new Cursor(storage_, qc, argv));
}

int SchedSliceTable::BestIndex(const QueryConstraints& qc,
                               BestIndexInfo* info) {
  bool is_time_constrained = false;
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    const auto& cs = qc.constraints()[i];
    if (cs.iColumn == Column::kTimestamp)
      is_time_constrained = true;
  }

  info->estimated_cost = is_time_constrained ? 10 : 10000;
  info->order_by_consumed = true;

  return SQLITE_OK;
}

SchedSliceTable::Cursor::Cursor(const TraceStorage* storage,
                                const QueryConstraints& query_constraints,
                                sqlite3_value** argv)
    : order_by_(query_constraints.order_by()), storage_(storage) {
  // Remove ordering on timestamp if it is the only ordering as we are already
  // sorted on TS. This makes span joining significantly faster.
  if (order_by_.size() == 1 && order_by_[0].iColumn == Column::kTimestamp &&
      !order_by_[0].desc) {
    order_by_.clear();
  }

  std::bitset<base::kMaxCpus> cpu_filter;
  cpu_filter.set();

  uint64_t min_ts = 0;
  uint64_t max_ts = kUint64Max;

  for (size_t i = 0; i < query_constraints.constraints().size(); i++) {
    const auto& cs = query_constraints.constraints()[i];
    switch (cs.iColumn) {
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

  const auto& slices = storage_->slices();
  const auto& start_ns = slices.start_ns();
  PERFETTO_CHECK(slices.slice_count() <= std::numeric_limits<uint32_t>::max());

  auto min_it = std::lower_bound(start_ns.begin(), start_ns.end(), min_ts);
  auto max_it = std::upper_bound(min_it, start_ns.end(), max_ts);
  ptrdiff_t dist = std::distance(min_it, max_it);
  PERFETTO_CHECK(dist >= 0 && static_cast<size_t>(dist) <= start_ns.size());

  auto start_idx = std::distance(start_ns.begin(), min_it);
  auto end_idx = std::distance(start_ns.begin(), max_it);

  std::vector<bool> row_filter(start_ns.size(), true);
  std::fill(row_filter.begin(), row_filter.begin() + start_idx, false);
  std::fill(row_filter.begin() + end_idx, row_filter.end(), false);

  for (size_t i = 0; i < query_constraints.constraints().size(); i++) {
    const auto& cs = query_constraints.constraints()[i];
    switch (cs.iColumn) {
      case Column::kTimestamp:
        break;
      case Column::kCpu:
        sqlite_utils::FilterColumn(slices.cpus(), cs, argv[i], &row_filter);
        break;
    }
  }

  auto set_bits = std::count(row_filter.begin() + start_idx,
                             row_filter.begin() + end_idx, true);

  // Fill |indices| with the consecutive row numbers affected by the filtering.
  sorted_row_ids_.resize(static_cast<size_t>(set_bits));
  size_t i = 0;
  auto it = std::find(row_filter.begin(), row_filter.end(), true);
  while (it != row_filter.end()) {
    auto index = std::distance(row_filter.begin(), it);
    sorted_row_ids_[i++] = static_cast<uint32_t>(index);
    it = std::find(it + 1, row_filter.end(), true);
  }

  // Sort if there is any order by constraints.
  if (!order_by_.empty()) {
    std::sort(
        sorted_row_ids_.begin(), sorted_row_ids_.end(),
        [this](uint32_t f, uint32_t s) { return CompareSlices(f, s) < 0; });
  }
}

int SchedSliceTable::Cursor::Next() {
  next_row_id_index_++;
  return SQLITE_OK;
}

int SchedSliceTable::Cursor::Eof() {
  return !IsNextRowIdIndexValid();
}

int SchedSliceTable::Cursor::Column(sqlite3_context* context, int N) {
  PERFETTO_DCHECK(IsNextRowIdIndexValid());

  size_t row = next_row_id();
  const auto& slices = storage_->slices();
  switch (N) {
    case Column::kTimestamp: {
      uint64_t ts = slices.start_ns()[row];
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(ts));
      break;
    }
    case Column::kCpu: {
      sqlite3_result_int(context, static_cast<int>(slices.cpus()[row]));
      break;
    }
    case Column::kDuration: {
      uint64_t duration = slices.durations()[row];
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(duration));
      break;
    }
    case Column::kUtid: {
      sqlite3_result_int64(context, slices.utids()[row]);
      break;
    }
  }
  return SQLITE_OK;
}

int SchedSliceTable::Cursor::CompareSlices(size_t f_idx, size_t s_idx) {
  for (const auto& ob : order_by_) {
    int c = CompareSlicesOnColumn(f_idx, s_idx, ob);
    if (c != 0)
      return c;
  }
  return 0;
}

int SchedSliceTable::Cursor::CompareSlicesOnColumn(
    size_t f_idx,
    size_t s_idx,
    const QueryConstraints::OrderBy& ob) {
  const auto& sl = storage_->slices();
  switch (ob.iColumn) {
    case SchedSliceTable::Column::kTimestamp:
      return Compare(sl.start_ns()[f_idx], sl.start_ns()[s_idx], ob.desc);
    case SchedSliceTable::Column::kDuration:
      return Compare(sl.durations()[f_idx], sl.durations()[s_idx], ob.desc);
    case SchedSliceTable::Column::kCpu:
      return Compare(sl.cpus()[f_idx], sl.cpus()[s_idx], ob.desc);
    case SchedSliceTable::Column::kUtid:
      return Compare(sl.utids()[f_idx], sl.utids()[s_idx], ob.desc);
  }
  PERFETTO_FATAL("Unexpected column %d", ob.iColumn);
}

}  // namespace trace_processor
}  // namespace perfetto
