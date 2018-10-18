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

#include "src/trace_processor/storage_cursor.h"

namespace perfetto {
namespace trace_processor {

StorageCursor::StorageCursor(Table::Schema schema,
                             std::unique_ptr<RowIterator> iterator,
                             std::unique_ptr<ValueRetriever> retriever)
    : schema_(std::move(schema)),
      iterator_(std::move(iterator)),
      retriever_(std::move(retriever)) {}

int StorageCursor::Next() {
  iterator_->NextRow();
  return SQLITE_OK;
}

int StorageCursor::Eof() {
  return iterator_->IsEnd();
}

int StorageCursor::Column(sqlite3_context* context, int raw_col) {
  uint32_t row = iterator_->Row();
  size_t column = static_cast<size_t>(raw_col);
  switch (schema_.columns()[static_cast<size_t>(column)].type()) {
    case Table::ColumnType::kUlong:
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(
                                        retriever_->GetUlong(column, row)));
      break;
    case Table::ColumnType::kUint:
      sqlite3_result_int64(context, retriever_->GetUint(column, row));
      break;
    case Table::ColumnType::kDouble:
      sqlite3_result_double(context, retriever_->GetDouble(column, row));
      break;
    case Table::ColumnType::kLong:
      sqlite3_result_int64(context, retriever_->GetLong(column, row));
      break;
    case Table::ColumnType::kInt:
      sqlite3_result_int(context, retriever_->GetInt(column, row));
      break;
    case Table::ColumnType::kString: {
      auto pair = retriever_->GetString(column, row);
      sqlite3_result_text(context, pair.first, -1, pair.second);
      break;
    }
  }
  return SQLITE_OK;
}

StorageCursor::RowIterator::~RowIterator() = default;
StorageCursor::ValueRetriever::~ValueRetriever() = default;

}  // namespace trace_processor
}  // namespace perfetto
