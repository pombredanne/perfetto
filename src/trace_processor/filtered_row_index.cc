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

#include "src/trace_processor/filtered_row_index.h"

#include <numeric>

namespace perfetto {
namespace trace_processor {

FilteredRowIndex::FilteredRowIndex(uint32_t start_row, uint32_t end_row)
    : mode_(Mode::kAllRows), start_row_(start_row), end_row_(end_row) {}

void FilteredRowIndex::IntersectRows(std::vector<uint32_t> rows) {
  // Sort the rows so all branches below make sense.
  std::sort(rows.begin(), rows.end());

  if (mode_ == kAllRows) {
    mode_ = Mode::kRowVector;
    rows_ = std::move(rows);
    return;
  } else if (mode_ == kRowVector) {
    std::vector<uint32_t> intersected;
    std::set_intersection(rows_.begin(), rows_.end(), rows.begin(), rows.end(),
                          std::back_inserter(intersected));
    rows_ = std::move(intersected);
    return;
  }

  // Initialise start to the beginning of the vector.
  auto start = row_filter_.begin();

  // Skip directly to the rows in range of start and end.
  size_t i = 0;
  for (; i < rows.size() && rows[i] < start_row_; i++) {
  }
  for (; i < rows.size() && rows[i] < end_row_; i++) {
    // Unset all bits between the start iterator and the iterator pointing
    // to the current row. That is, this loop sets all elements not pointed
    // to by rows to false. It does not touch the rows themselves which
    // means if they were already false (i.e. not returned) then they won't
    // be returned now and if they were true (i.e. returned) they will still
    // be returned.
    auto row = rows[i];
    auto end = row_filter_.begin() + static_cast<ptrdiff_t>(row - start_row_);
    std::fill(start, end, false);
    start = end + 1;
  }
  std::fill(start, row_filter_.end(), false);
}

std::vector<bool> FilteredRowIndex::TakeBitvector() {
  switch (mode_) {
    case Mode::kAllRows:
      row_filter_.resize(end_row_ - start_row_, true);
      break;
    case Mode::kRowVector: {
      row_filter_.resize(end_row_ - start_row_, false);
      size_t i = 0;
      for (; i < rows_.size() && rows_[i] < start_row_; i++) {
      }
      for (; i < rows_.size() && rows_[i] < end_row_; i++) {
        row_filter_[rows_[i] - start_row_] = true;
      }
      break;
    }
    case Mode::kBitVector:
      // Nothing to do.
      break;
  }
  auto vector = std::move(row_filter_);
  row_filter_.clear();
  mode_ = Mode::kAllRows;
  return vector;
}

std::vector<uint32_t> FilteredRowIndex::TakeRowVector() {
  switch (mode_) {
    case Mode::kAllRows:
      rows_.resize(end_row_ - start_row_);
      std::iota(rows_.begin(), rows_.end(), 0);
      break;
    case Mode::kBitVector:
      ConvertBitVectorToRowVector();
      break;
    case Mode::kRowVector:
      // Nothing to do.
      break;
  }
  auto vector = std::move(rows_);
  rows_.clear();
  mode_ = Mode::kAllRows;
  return vector;
}

void FilteredRowIndex::ConvertBitVectorToRowVector() {
  mode_ = Mode::kRowVector;

  auto b = row_filter_.begin();
  auto e = row_filter_.end();
  using std::find;
  for (auto it = find(b, e, true); it != e; it = find(it + 1, e, true)) {
    auto filter_idx = static_cast<uint32_t>(std::distance(b, it));
    rows_.emplace_back(filter_idx + start_row_);
  }
  row_filter_.clear();
}

}  // namespace trace_processor
}  // namespace perfetto
