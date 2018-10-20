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
#include "src/trace_processor/table_utils.h"

namespace perfetto {
namespace trace_processor {

SchedSliceTable::SchedSliceTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void SchedSliceTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<SchedSliceTable>(db, storage, "sched");
}

Table::Schema SchedSliceTable::CreateSchema(int, const char* const*) {
  const auto& slices = storage_->slices();

  columns_.emplace_back(StorageCursor::NumericColumnPtr(
      "ts", &slices.start_ns(), false /* hidden */, true /* ordered */));
  columns_.emplace_back(StorageCursor::NumericColumnPtr("cpu", &slices.cpus()));
  columns_.emplace_back(
      StorageCursor::NumericColumnPtr("duration", &slices.durations()));
  columns_.emplace_back(
      StorageCursor::NumericColumnPtr("utid", &slices.utids()));
  return table_utils::CreateSchemaFromStorageColumns(columns_, {"cpu", "ts"});
}

std::unique_ptr<Table::Cursor> SchedSliceTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  uint32_t count = static_cast<uint32_t>(storage_->slices().slice_count());
  auto row_it =
      table_utils::CreateOptimalRowIterator(columns_, count, qc, argv);

  std::vector<StorageCursor::ColumnDefn*> defns;
  for (const auto& col : columns_)
    defns.emplace_back(col.get());
  return std::unique_ptr<Table::Cursor>(
      new StorageCursor(std::move(row_it), std::move(defns)));
}

int SchedSliceTable::BestIndex(const QueryConstraints& qc,
                               BestIndexInfo* info) {
  const auto& cs = qc.constraints();
  size_t ts_idx = table_utils::ColumnIndexFromName(columns_, "ts");
  auto predicate = [ts_idx](const QueryConstraints::Constraint& c) {
    return c.iColumn == static_cast<int>(ts_idx);
  };
  bool is_time_constrained = std::any_of(cs.begin(), cs.end(), predicate);
  bool has_time_constraint = !qc.constraints().empty() && is_time_constrained;
  info->estimated_cost = has_time_constraint ? 10 : 10000;

  // We should be able to handle any constraint and any order by clause given
  // to us.
  info->order_by_consumed = true;
  std::fill(info->omit.begin(), info->omit.end(), true);

  return SQLITE_OK;
}

}  // namespace trace_processor
}  // namespace perfetto
