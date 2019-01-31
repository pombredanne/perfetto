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

#include "src/trace_processor/async_slice_table.h"

#include <sqlite3.h>
#include <string.h>

#include <algorithm>
#include <bitset>
#include <numeric>

#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

AsyncSliceTable::AsyncSliceTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void AsyncSliceTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<AsyncSliceTable>(db, storage, "async_slices");
}

Table::Schema AsyncSliceTable::CreateSchema(int, const char* const*) {
  return Schema(
      {
          Table::Column(Column::kTimestamp, "ts", ColumnType::kUlong),
          Table::Column(Column::kDuration, "dur", ColumnType::kUlong),
          Table::Column(Column::kUpid, "upid", ColumnType::kUint),
          Table::Column(Column::kCategory, "cat", ColumnType::kString),
          Table::Column(Column::kName, "name", ColumnType::kString),
          Table::Column(Column::kAsyncId, "async_id ", ColumnType::kString),
      },
      {Column::kUpid, Column::kCategory, Column::kAsyncId, Column::kTimestamp});
}

std::unique_ptr<Table::Cursor> AsyncSliceTable::CreateCursor(const QueryConstraints&,
                                                        sqlite3_value**) {
  return std::unique_ptr<Table::Cursor>(new Cursor(storage_));
}

int AsyncSliceTable::BestIndex(const QueryConstraints&, BestIndexInfo* info) {
  info->order_by_consumed = false;  // Delegate sorting to SQLite.
  info->estimated_cost =
      static_cast<uint32_t>(storage_->async_slices().slice_count());
  return SQLITE_OK;
}

AsyncSliceTable::Cursor::Cursor(const TraceStorage* storage) : storage_(storage) {
  num_rows_ = storage->async_slices().slice_count();
}

AsyncSliceTable::Cursor::~Cursor() = default;

int AsyncSliceTable::Cursor::Next() {
  row_++;
  return SQLITE_OK;
}

int AsyncSliceTable::Cursor::Eof() {
  return row_ >= num_rows_;
}

int AsyncSliceTable::Cursor::Column(sqlite3_context* context, int col) {
  const auto& slices = storage_->async_slices();
  switch (col) {
    case Column::kTimestamp:
      sqlite3_result_int64(context,
                           static_cast<sqlite3_int64>(slices.start_ns()[row_]));
      break;
    case Column::kDuration:
      sqlite3_result_int64(
          context, static_cast<sqlite3_int64>(slices.durations()[row_]));
      break;
    case Column::kUpid:
      sqlite3_result_int64(context,
                           static_cast<sqlite3_int64>(slices.upids()[row_]));
      break;
    case Column::kCategory:
      sqlite3_result_text(context,
                          storage_->GetString(slices.cats()[row_]).c_str(), -1,
                          nullptr);
      break;
    case Column::kName:
      sqlite3_result_text(context,
                          storage_->GetString(slices.names()[row_]).c_str(), -1,
                          nullptr);
      break;
    case Column::kAsyncId:
      sqlite3_result_text(
          context, slices.async_ids()[row_].c_str(), -1, nullptr);
      break;
  }
  return SQLITE_OK;
}

}  // namespace trace_processor
}  // namespace perfetto
