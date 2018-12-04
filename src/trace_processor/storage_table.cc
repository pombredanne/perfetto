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

#include "src/trace_processor/args_table.h"

#include "src/trace_processor/storage_cursor.h"
#include "src/trace_processor/table_utils.h"

namespace perfetto {
namespace trace_processor {

StorageTable::StorageTable(
    const TraceStorage* storage,
    std::vector<std::unique_ptr<StorageSchema::Column>> columns,
    std::vector<std::string> primary_keys)
    : storage_(storage),
      schema_(std::move(columns)),
      primary_keys_(std::move(primary_keys)) {}

Table::Schema StorageTable::CreateSchema(int, const char* const*) {
  return schema_.ToTableSchema(primary_keys_);
}

std::unique_ptr<Table::Cursor> StorageTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  uint32_t count = static_cast<uint32_t>(storage_->args().args_count());
  auto it = table_utils::CreateBestRowIteratorForGenericSchema(schema_, count,
                                                               qc, argv);
  return std::unique_ptr<Table::Cursor>(
      new StorageCursor(std::move(it), schema_.ToColumnReporters()));
}

}  // namespace trace_processor
}  // namespace perfetto
