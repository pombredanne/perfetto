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

constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();

// Compares the slice at index |f| with the slice at index |s| on the
// criteria in |order_by|.
// Returns -1 if the first slice is before the second in the ordering, 1 if
// the first slice is after the second and 0 if they are equal.
PERFETTO_ALWAYS_INLINE int CompareSlicesOnColumn(
    const TraceStorage* storage,
    size_t f_idx,
    size_t s_idx,
    const QueryConstraints::OrderBy& ob) {
  const auto& sl = storage->slices();
  switch (ob.iColumn) {
    case SchedSliceTable::Column::kTimestamp:
      return sqlite_utils::CompareValues(sl.start_ns(), f_idx, s_idx, ob.desc);
    case SchedSliceTable::Column::kDuration:
      return sqlite_utils::CompareValues(sl.durations(), f_idx, s_idx, ob.desc);
    case SchedSliceTable::Column::kCpu:
      return sqlite_utils::CompareValues(sl.cpus(), f_idx, s_idx, ob.desc);
    case SchedSliceTable::Column::kUtid:
      return sqlite_utils::CompareValues(sl.utids(), f_idx, s_idx, ob.desc);
    default:
      PERFETTO_FATAL("Unexpected column %d", ob.iColumn);
  }
}

// Compares the slice at index |f| with the slice at index |s|on all
// columns.
// Returns -1 if the first slice is before the second in the ordering, 1 if
// the first slice is after the second and 0 if they are equal.
PERFETTO_ALWAYS_INLINE int CompareSlices(
    const TraceStorage* storage,
    size_t f_idx,
    size_t s_idx,
    const std::vector<QueryConstraints::OrderBy>& order_by) {
  for (const auto& ob : order_by) {
    int c = CompareSlicesOnColumn(storage, f_idx, s_idx, ob);
    if (c != 0)
      return c;
  }
  return 0;
}

std::pair<uint64_t, uint64_t> GetTsBounds(const QueryConstraints& qc,
                                          sqlite3_value** argv) {
  uint64_t min_ts = 0;
  uint64_t max_ts = kUint64Max;
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    const auto& cs = qc.constraints()[i];
    switch (cs.iColumn) {
      case SchedSliceTable::Column::kTimestamp:
        auto ts = static_cast<uint64_t>(sqlite3_value_int64(argv[i]));
        if (sqlite_utils::IsOpGe(cs.op) || sqlite_utils::IsOpGt(cs.op)) {
          min_ts = sqlite_utils::IsOpGe(cs.op) ? ts : ts + 1;
        } else if (sqlite_utils::IsOpLe(cs.op) || sqlite_utils::IsOpLt(cs.op)) {
          max_ts = sqlite_utils::IsOpLe(cs.op) ? ts : ts - 1;
        } else if (sqlite_utils::IsOpEq(cs.op)) {
          min_ts = ts;
          max_ts = ts;
        } else {
          // We can't handle any other constraints on ts.
          PERFETTO_CHECK(false);
        }
        break;
    }
  }
  return std::make_pair(min_ts, max_ts);
}

std::pair<uint32_t, uint32_t> FindTsIndices(
    const TraceStorage* storage,
    std::pair<uint64_t, uint64_t> ts_bounds) {
  const auto& slices = storage->slices();
  const auto& ts = slices.start_ns();
  PERFETTO_CHECK(slices.slice_count() <= std::numeric_limits<uint32_t>::max());

  auto min_it = std::lower_bound(ts.begin(), ts.end(), ts_bounds.first);
  auto min_idx = static_cast<uint32_t>(std::distance(ts.begin(), min_it));

  auto max_it = std::upper_bound(min_it, ts.end(), ts_bounds.second);
  auto max_idx = static_cast<uint32_t>(std::distance(ts.begin(), max_it));

  return std::make_pair(min_idx, max_idx);
}

bool HasOnlyTsConstraints(const QueryConstraints& qc) {
  auto fn = [](const QueryConstraints::Constraint& c) {
    return c.iColumn == SchedSliceTable::Column::kTimestamp;
  };
  return std::all_of(qc.constraints().begin(), qc.constraints().end(), fn);
}

bool IsTsOrdered(const QueryConstraints& qc) {
  return qc.order_by().size() == 0 ||
         (qc.order_by().size() == 1 &&
          qc.order_by()[0].iColumn == SchedSliceTable::Column::kTimestamp);
}

std::unique_ptr<StorageCursor::RowIterator> CreateIterator(
    const TraceStorage* storage,
    const StorageCursor::ValueRetriever& retriever,
    const Table::Schema& schema,
    const QueryConstraints& qc,
    sqlite3_value** argv,
    std::pair<uint32_t, uint32_t> bounding_indices) {
  auto min_idx = bounding_indices.first;
  auto max_idx = bounding_indices.second;
  auto comparator = [storage, &qc](uint32_t f, uint32_t s) {
    return CompareSlices(storage, f, s, qc.order_by()) < 0;
  };

  if (HasOnlyTsConstraints(qc)) {
    if (IsTsOrdered(qc)) {
      bool desc = qc.order_by().size() == 1 && qc.order_by()[0].desc;
      return std::unique_ptr<FilteredRowIterator>(
          new FilteredRowIterator(min_idx, max_idx, desc));
    }

    std::vector<uint32_t> sorted(static_cast<size_t>(max_idx - min_idx));
    std::iota(sorted.begin(), sorted.end(), min_idx);
    std::sort(sorted.begin(), sorted.end(), comparator);
    return std::unique_ptr<SortedRowIterator>(
        new SortedRowIterator(std::move(sorted)));
  }

  auto filter = sqlite_utils::CreateFilterVector(retriever, schema, qc, argv,
                                                 min_idx, max_idx);
  if (IsTsOrdered(qc)) {
    bool desc = qc.order_by().size() == 1 && qc.order_by()[0].desc;
    return std::unique_ptr<FilteredRowIterator>(
        new FilteredRowIterator(min_idx, desc, std::move(filter)));
  }
  auto sorted =
      sqlite_utils::CreateSortedIndexFromFilter(min_idx, filter, comparator);
  return std::unique_ptr<SortedRowIterator>(
      new SortedRowIterator(std::move(sorted)));
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
  auto ts_indices = FindTsIndices(storage_, GetTsBounds(qc, argv));
  std::unique_ptr<ValueRetriever> retriver(new ValueRetriever(storage_));
  auto row_it =
      CreateIterator(storage_, *retriver, schema(), qc, argv, ts_indices);
  return std::unique_ptr<Table::Cursor>(
      new StorageCursor(schema(), std::move(row_it), std::move(retriver)));
}

int SchedSliceTable::BestIndex(const QueryConstraints& qc,
                               BestIndexInfo* info) {
  bool is_time_constrained =
      !qc.constraints().empty() && HasOnlyTsConstraints(qc);
  info->estimated_cost = is_time_constrained ? 10 : 10000;
  info->order_by_consumed = true;

  // We should be able to handle any constraint thrown at us.
  std::fill(info->omit.begin(), info->omit.end(), true);

  return SQLITE_OK;
}

SchedSliceTable::ValueRetriever::ValueRetriever(const TraceStorage* storage)
    : storage_(storage) {}

uint32_t SchedSliceTable::ValueRetriever::GetUint(size_t column,
                                                  uint32_t row) const {
  const auto& slices = storage_->slices();
  switch (column) {
    case Column::kCpu:
      return slices.cpus()[row];
    case Column::kUtid:
      return slices.utids()[row];
    default:
      PERFETTO_FATAL("Unknown column requested");
  }
}

uint64_t SchedSliceTable::ValueRetriever::GetUlong(size_t column,
                                                   uint32_t row) const {
  const auto& slices = storage_->slices();
  switch (column) {
    case Column::kTimestamp:
      return slices.start_ns()[row];
    case Column::kDuration:
      return slices.durations()[row];
    default:
      PERFETTO_FATAL("Unknown column requested");
  }
}

}  // namespace trace_processor
}  // namespace perfetto
