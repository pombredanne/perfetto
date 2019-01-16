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

namespace {}

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
      .AddColumn<EndReasonColumn>("end_reason", &slices.end_state())
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
  // TODO(lalitm): add support for ordering by and filtering end_reason.
  info->order_by_consumed = false;
  size_t end_reason_index = schema().ColumnIndexFromName("end_reason");
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    info->omit[i] =
        qc.constraints()[i].iColumn != static_cast<int>(end_reason_index);
  }
  return SQLITE_OK;
}

SchedSliceTable::EndReasonColumn::EndReasonColumn(
    std::string col_name,
    std::deque<ftrace_utils::TaskState>* deque)
    : StorageColumn(col_name, false), deque_(deque) {}
SchedSliceTable::EndReasonColumn::~EndReasonColumn() = default;

void SchedSliceTable::EndReasonColumn::ReportResult(sqlite3_context* ctx,
                                                    uint32_t row) const {
  const auto& state = (*deque_)[row];
  char buffer[4];
  state.ToString(buffer, sizeof(buffer));
  sqlite3_result_text(ctx, buffer, -1, sqlite_utils::kSqliteTransient);
}

void SchedSliceTable::EndReasonColumn::Filter(int,
                                              sqlite3_value*,
                                              FilteredRowIndex*) const {
  // TODO(lalitm): implement this.
}

StorageColumn::Comparator SchedSliceTable::EndReasonColumn::Sort(
    const QueryConstraints::OrderBy&) const {
  // TODO(lalitm): implement this.
  return [](uint32_t, uint32_t) { return false; };
}

Table::ColumnType SchedSliceTable::EndReasonColumn::GetType() const {
  return Table::ColumnType::kString;
}

}  // namespace trace_processor
}  // namespace perfetto
