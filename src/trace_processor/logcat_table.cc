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

#include "src/trace_processor/logcat_table.h"

namespace perfetto {
namespace trace_processor {

LogcatTable::LogcatTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage) {}

void LogcatTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<LogcatTable>(db, storage, "logcat");
}

base::Optional<Table::Schema> LogcatTable::Init(int, const char* const*) {
  const auto& logcat = storage_->logcat();
  std::unique_ptr<StorageColumn> cols[] = {
      NumericColumnPtr("ts", &logcat.timestamps(), false /* hidden */,
                       true /* ordered */),
      NumericColumnPtr("utid", &logcat.utids(), false, true),
      NumericColumnPtr("prio", &logcat.prios(), false, true),
      StringColumnPtr("tag", &logcat.tag_ids(), &storage_->string_pool()),
      StringColumnPtr("msg", &logcat.msg_ids(), &storage_->string_pool())};
  schema_ = StorageSchema({
      std::make_move_iterator(std::begin(cols)),
      std::make_move_iterator(std::end(cols)),
  });
  return schema_.ToTableSchema({"ts", "utid", "msg"});
}

std::unique_ptr<Table::Cursor> LogcatTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  uint32_t count = static_cast<uint32_t>(storage_->logcat().size());
  auto it = CreateBestRowIteratorForGenericSchema(count, qc, argv);
  return std::unique_ptr<Table::Cursor>(
      new Cursor(std::move(it), schema_.mutable_columns()));
}

int LogcatTable::BestIndex(const QueryConstraints& qc, BestIndexInfo* info) {
  info->estimated_cost =
      static_cast<uint32_t>(storage_->counters().counter_count());

  // TODO think more here. // DNS .

  // Only the string columns are handled by SQLite
  info->order_by_consumed = true;
  size_t tag_index = schema_.ColumnIndexFromName("tag");
  size_t msg_index = schema_.ColumnIndexFromName("msg");
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    info->omit[i] =
        qc.constraints()[i].iColumn != static_cast<int>(tag_index) &&
        qc.constraints()[i].iColumn != static_cast<int>(msg_index);
  }

  return SQLITE_OK;
}
}  // namespace trace_processor
}  // namespace perfetto
