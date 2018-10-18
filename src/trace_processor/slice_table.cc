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

#include "src/trace_processor/slice_table.h"

#include <sqlite3.h>
#include <string.h>

#include <algorithm>
#include <bitset>
#include <numeric>

#include "src/trace_processor/row_iterators.h"
#include "src/trace_processor/sqlite_utils.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

namespace {

std::pair<uint32_t, uint32_t> FindTsIndices(
    const TraceStorage* storage,
    std::pair<uint64_t, uint64_t> ts_bounds) {
  const auto& slices = storage->nestable_slices();
  const auto& ts = slices.start_ns();
  PERFETTO_CHECK(slices.slice_count() <= std::numeric_limits<uint32_t>::max());

  auto min_it = std::lower_bound(ts.begin(), ts.end(), ts_bounds.first);
  auto min_idx = static_cast<uint32_t>(std::distance(ts.begin(), min_it));

  auto max_it = std::upper_bound(min_it, ts.end(), ts_bounds.second);
  auto max_idx = static_cast<uint32_t>(std::distance(ts.begin(), max_it));

  return std::make_pair(min_idx, max_idx);
}

}  // namespace

SliceTable::SliceTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void SliceTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<SliceTable>(db, storage, "slices");
}

Table::Schema SliceTable::CreateSchema(int, const char* const*) {
  return Schema(
      {
          Table::Column(Column::kTimestamp, "ts", ColumnType::kUlong),
          Table::Column(Column::kDuration, "dur", ColumnType::kUlong),
          Table::Column(Column::kUtid, "utid", ColumnType::kUint),
          Table::Column(Column::kCategory, "cat", ColumnType::kString),
          Table::Column(Column::kName, "name", ColumnType::kString),
          Table::Column(Column::kDepth, "depth", ColumnType::kUint),
          Table::Column(Column::kStackId, "stack_id", ColumnType::kUlong),
          Table::Column(Column::kParentStackId, "parent_stack_id",
                        ColumnType::kUlong),
          Table::Column(Column::kCpu, "cpu", ColumnType::kUint),
      },
      {Column::kUtid, Column::kTimestamp, Column::kDepth});
}

std::unique_ptr<Table::Cursor> SliceTable::CreateCursor(
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

int SliceTable::BestIndex(const QueryConstraints&, BestIndexInfo* info) {
  info->order_by_consumed = true;
  info->estimated_cost =
      static_cast<uint32_t>(storage_->nestable_slices().slice_count());
  return SQLITE_OK;
}

SliceTable::ValueRetriever::ValueRetriever(const TraceStorage* storage)
    : storage_(storage) {}

SliceTable::ValueRetriever::StringAndDestructor
SliceTable::ValueRetriever::GetString(size_t column, uint32_t row) const {
  const auto& counters = storage_->nestable_slices();
  const char* string = nullptr;
  switch (column) {
    case Column::kCategory:
      string = storage_->GetString(counters.cats()[row]).c_str();
      break;
    case Column::kName:
      string = storage_->GetString(counters.names()[row]).c_str();
      break;
    default:
      PERFETTO_FATAL("Unknown column requested");
  }
  auto kStaticDestructor = static_cast<sqlite3_destructor_type>(0);
  return std::make_pair(string, kStaticDestructor);
}

uint32_t SliceTable::ValueRetriever::GetUint(size_t column,
                                             uint32_t row) const {
  const auto& slices = storage_->nestable_slices();
  switch (column) {
    case Column::kUtid:
      return slices.utids()[row];
    case Column::kDepth:
      return slices.depths()[row];
    case Column::kCpu:
      return 0;
    default:
      PERFETTO_FATAL("Unknown column requested");
  }
}

uint64_t SliceTable::ValueRetriever::GetUlong(size_t column,
                                              uint32_t row) const {
  const auto& slices = storage_->nestable_slices();
  switch (column) {
    case Column::kTimestamp:
      return slices.start_ns()[row];
    case Column::kDuration:
      return slices.durations()[row];
    case Column::kStackId:
      return slices.stack_ids()[row];
    case Column::kParentStackId:
      return slices.parent_stack_ids()[row];
    default:
      PERFETTO_FATAL("Unknown column requested");
  }
}

}  // namespace trace_processor
}  // namespace perfetto
