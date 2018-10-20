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

#ifndef SRC_TRACE_PROCESSOR_ROW_ITERATORS_H_
#define SRC_TRACE_PROCESSOR_ROW_ITERATORS_H_

#include <stddef.h>
#include <vector>

#include "src/trace_processor/sqlite_utils.h"
#include "src/trace_processor/storage_cursor.h"

namespace perfetto {
namespace trace_processor {

class FilteredRowIterator : public StorageCursor::RowIterator {
 public:
  FilteredRowIterator(uint32_t start_row, uint32_t end_row, bool desc);
  FilteredRowIterator(uint32_t start_row,
                      bool desc,
                      std::vector<bool> row_filter);

  void NextRow() override;
  bool IsEnd() override { return offset_ >= end_row_ - start_row_; }
  uint32_t Row() override {
    return desc_ ? end_row_ - offset_ - 1 : start_row_ + offset_;
  }

  uint32_t RowCount() const {
    return row_filter_.empty()
               ? end_row_ - start_row_
               : static_cast<uint32_t>(
                     std::count(row_filter_.begin(), row_filter_.end(), true));
  }

 private:
  uint32_t start_row_ = 0;
  uint32_t end_row_ = 0;
  bool desc_ = false;
  std::vector<bool> row_filter_;

  // In non-desc mode, this is an offset from start_row_ while in desc mode,
  // this is an offset from end_row_.
  uint32_t offset_ = 0;
};

class SortedRowIterator : public StorageCursor::RowIterator {
 public:
  SortedRowIterator(std::vector<uint32_t> sorted_rows);
  ~SortedRowIterator() override;

  void NextRow() override { next_row_idx_++; }
  bool IsEnd() override { return next_row_idx_ >= sorted_rows_.size(); }
  uint32_t Row() override { return sorted_rows_[next_row_idx_]; }

 private:
  // Vector of row ids sorted by some order by constraints.
  std::vector<uint32_t> sorted_rows_;

  // An offset into |sorted_row_ids_| indicating the next row to return.
  uint32_t next_row_idx_ = 0;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_ROW_ITERATORS_H_
