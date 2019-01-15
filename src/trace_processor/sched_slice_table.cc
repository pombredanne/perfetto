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

namespace perfetto {
namespace trace_processor {

SchedSliceTable::SchedSliceTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void SchedSliceTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<SchedSliceTable>(db, storage, "sched");
}

StorageSchema SchedSliceTable::CreateStorageSchema() {
  const auto& slices = storage_->slices();
  return StorageSchema::Builder()
      .AddOrderedNumericColumn("ts", &slices.start_ns())
      .AddNumericColumn("cpu", &slices.cpus())
      .AddNumericColumn("dur", &slices.durations())
      .AddColumn<TsEndColumn>("ts_end", &slices.start_ns(), &slices.durations())
      .AddNumericColumn("utid", &slices.utids())
      .AddColumn<SchedReasonColumn>("end_reason", &slices.end_reasons())
      .AddNumericColumn("priority", &slices.priorities())
      .Build({"cpu", "ts"});
}

uint32_t SchedSliceTable::RowCount() {
  return static_cast<uint32_t>(storage_->slices().slice_count());
}

int SchedSliceTable::BestIndex(const QueryConstraints& qc,
                               BestIndexInfo* info) {
  const auto& cs = qc.constraints();
  size_t ts_idx = schema().ColumnIndexFromName("ts");
  auto has_ts_column = [ts_idx](const QueryConstraints::Constraint& c) {
    return c.iColumn == static_cast<int>(ts_idx);
  };
  bool has_time_constraint = std::any_of(cs.begin(), cs.end(), has_ts_column);
  info->estimated_cost = has_time_constraint ? 10 : 10000;

  // We should be able to handle any constraint and any order by clause given
  // to us.
  info->order_by_consumed = true;
  size_t end_reason_index = schema().ColumnIndexFromName("end_reason");
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    info->omit[i] =
        qc.constraints()[i].iColumn != static_cast<int>(end_reason_index);
  }
  return SQLITE_OK;
}

SchedSliceTable::SchedReasonColumn::SchedReasonColumn(
    std::string col_name,
    const std::deque<SchedReason>* deque)
    : StorageColumn(col_name, false), deque_(deque) {}

void SchedSliceTable::SchedReasonColumn::ReportResult(sqlite3_context* ctx,
                                                      uint32_t row) const {
  sqlite3_result_text(ctx, (*deque_)[row].data(), -1,
                      sqlite_utils::kSqliteStatic);
}

SchedSliceTable::SchedReasonColumn::Bounds
SchedSliceTable::SchedReasonColumn::BoundFilter(int, sqlite3_value*) const {
  Bounds bounds;
  bounds.max_idx = static_cast<uint32_t>(deque_->size());
  return bounds;
}

void SchedSliceTable::SchedReasonColumn::Filter(int,
                                                sqlite3_value*,
                                                FilteredRowIndex*) const {}

SchedSliceTable::SchedReasonColumn::Comparator
SchedSliceTable::SchedReasonColumn::Sort(
    const QueryConstraints::OrderBy& ob) const {
  constexpr size_t kSchedSize = std::tuple_size<SchedReason>();
  if (ob.desc) {
    return [this](uint32_t f, uint32_t s) {
      const SchedReason& a = (*deque_)[f];
      const SchedReason& b = (*deque_)[s];
      return sqlite_utils::CompareValuesAsc(a.data(), b.data(), kSchedSize);
    };
  }
  return [this](uint32_t f, uint32_t s) {
    const SchedReason& a = (*deque_)[f];
    const SchedReason& b = (*deque_)[s];
    return sqlite_utils::CompareValuesDesc(a.data(), b.data(), kSchedSize);
  };
}

}  // namespace trace_processor
}  // namespace perfetto
