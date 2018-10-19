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

#include "perfetto/base/logging.h"
#include "src/trace_processor/query_constraints.h"
#include "src/trace_processor/row_iterators.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

std::pair<uint32_t, uint32_t> FindTsIndices(
    const TraceStorage* storage,
    std::pair<uint64_t, uint64_t> ts_bounds) {
  const auto& counters = storage->counters();
  const auto& ts = counters.timestamps();
  PERFETTO_CHECK(counters.counter_count() <=
                 std::numeric_limits<uint32_t>::max());

  auto min_it = std::lower_bound(ts.begin(), ts.end(), ts_bounds.first);
  auto min_idx = static_cast<uint32_t>(std::distance(ts.begin(), min_it));

  auto max_it = std::upper_bound(min_it, ts.end(), ts_bounds.second);
  auto max_idx = static_cast<uint32_t>(std::distance(ts.begin(), max_it));

  return std::make_pair(min_idx, max_idx);
}

}  // namespace

CountersTable::CountersTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void CountersTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<CountersTable>(db, storage, "counters");
}

Table::Schema CountersTable::CreateSchema(int, const char* const*) {
  return Schema(
      {
          Table::Column(Column::kTimestamp, "ts", ColumnType::kUlong),
          Table::Column(Column::kName, "name", ColumnType::kString),
          Table::Column(Column::kValue, "value", ColumnType::kUlong),
          Table::Column(Column::kDuration, "dur", ColumnType::kUlong),
          Table::Column(Column::kValueDelta, "value_delta", ColumnType::kUlong),
          Table::Column(Column::kRef, "ref", ColumnType::kLong),
          Table::Column(Column::kRefType, "ref_type", ColumnType::kString),
      },
      {Column::kName, Column::kTimestamp, Column::kRef});
}

std::unique_ptr<Table::Cursor> CountersTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  auto ts_bounds = sqlite_utils::GetBoundsForNumericColumn<uint64_t>(
      qc, argv, Column::kTimestamp);
  auto ts_indices = FindTsIndices(storage_, ts_bounds);

  std::unique_ptr<ValueRetriever> retr(new ValueRetriever(storage_));
  auto row_it = CreateOptimalRowIterator(schema(), *retr, Column::kTimestamp,
                                         ts_indices, qc, argv);
  return std::unique_ptr<Table::Cursor>(
      new StorageCursor(schema(), std::move(row_it), std::move(retr)));
}

int CountersTable::BestIndex(const QueryConstraints&, BestIndexInfo* info) {
  info->estimated_cost =
      static_cast<uint32_t>(storage_->counters().counter_count());

  // We should be able to handle any constraint and any order by clause given
  // to us.
  info->order_by_consumed = true;
  std::fill(info->omit.begin(), info->omit.end(), true);

  return SQLITE_OK;
}

CountersTable::ValueRetriever::ValueRetriever(const TraceStorage* storage)
    : storage_(storage) {}

CountersTable::ValueRetriever::StringAndDestructor
CountersTable::ValueRetriever::GetString(size_t column, uint32_t row) const {
  const auto& counters = storage_->counters();
  const char* string = nullptr;
  switch (column) {
    case Column::kName:
      string = storage_->GetString(counters.name_ids()[row]).c_str();
      break;
    case Column::kRefType:
      switch (storage_->counters().types()[row]) {
        case RefType::kCPU_ID:
          string = "cpu";
          break;
        case RefType::kUTID:
          string = "utid";
          break;
        case RefType::kNoRef:
          string = nullptr;
          break;
        case RefType::kIrq:
          string = "irq";
          break;
        case RefType::kSoftIrq:
          string = "softirq";
          break;
      }
      break;
    default:
      PERFETTO_FATAL("Unknown column requested");
  }
  auto kStaticDestructor = static_cast<sqlite3_destructor_type>(0);
  return std::make_pair(string, kStaticDestructor);
}

int64_t CountersTable::ValueRetriever::GetLong(size_t column,
                                               uint32_t row) const {
  const auto& counters = storage_->counters();
  switch (column) {
    case Column::kRef:
      return counters.refs()[row];
    default:
      PERFETTO_FATAL("Unknown column requested");
  }
}

uint64_t CountersTable::ValueRetriever::GetUlong(size_t column,
                                                 uint32_t row) const {
  const auto& counters = storage_->counters();
  switch (column) {
    case Column::kTimestamp:
      return counters.timestamps()[row];
    case Column::kDuration:
      return counters.durations()[row];
    case Column::kValue:
      return static_cast<uint64_t>(counters.values()[row]);
    case Column::kValueDelta:
      return static_cast<uint64_t>(counters.value_deltas()[row]);
    default:
      PERFETTO_FATAL("Unknown column requested");
  }
}

}  // namespace trace_processor
}  // namespace perfetto
