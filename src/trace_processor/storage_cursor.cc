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

StorageCursor::StorageCursor(std::unique_ptr<RowIterator> iterator,
                             std::vector<ColumnDefn*> cols)
    : iterator_(std::move(iterator)), columns_(std::move(cols)) {}

int StorageCursor::Next() {
  iterator_->NextRow();
  return SQLITE_OK;
}

int StorageCursor::Eof() {
  return iterator_->IsEnd();
}

int StorageCursor::Column(sqlite3_context* context, int raw_col) {
  // uint32_t row = iterator_->Row();
  size_t column = static_cast<size_t>(raw_col);
  switch (columns_[column]->GetType()) {
    case Table::ColumnType::kUlong:
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(0));
      break;
    case Table::ColumnType::kUint:
      sqlite3_result_int64(context, 0);
      break;
    case Table::ColumnType::kDouble:
      sqlite3_result_double(context, 0);
      break;
    case Table::ColumnType::kLong:
      sqlite3_result_int64(context, 0);
      break;
    case Table::ColumnType::kInt:
      sqlite3_result_int(context, 0);
      break;
    case Table::ColumnType::kString: {
      auto pair =
          std::make_pair((nullptr), static_cast<sqlite3_destructor_type>(0));
      if (pair.first == nullptr) {
        sqlite3_result_null(context);
        // } else {
        // sqlite3_result_text(context, pair.first, -1, pair.second);
      }
      break;
    }
  }
  return SQLITE_OK;
}

StorageCursor::RowIterator::~RowIterator() = default;

StorageCursor::ColumnDefn::ColumnDefn(std::string col_name, bool hidden)
    : col_name_(col_name), hidden_(hidden) {}
StorageCursor::ColumnDefn::~ColumnDefn() = default;

}  // namespace trace_processor
}  // namespace perfetto
