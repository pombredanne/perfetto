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
  auto ts_bounds = sqlite_utils::GetBoundsForNumericColumn<uint64_t>(
      qc, argv, Column::kTimestamp);
  auto ts_indices = FindTsIndices(storage_, ts_bounds);

  std::unique_ptr<ValueRetriever> retriver(new ValueRetriever(storage_));
  auto row_it = CreateOptimalRowIterator(
      schema(), *retriver, Column::kTimestamp, ts_indices, qc, argv);

  return std::unique_ptr<Table::Cursor>(
      new StorageCursor(schema(), std::move(row_it), std::move(retriver)));
}

int SchedSliceTable::BestIndex(const QueryConstraints& qc,
                               BestIndexInfo* info) {
  bool is_time_constrained =
      !qc.constraints().empty() &&
      sqlite_utils::HasOnlyConstraintsForColumn(qc, Column::kTimestamp);

  info->estimated_cost = is_time_constrained ? 10 : 10000;

  // We should be able to handle any constraint and any order by clause given
  // to us.
  info->order_by_consumed = true;
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
