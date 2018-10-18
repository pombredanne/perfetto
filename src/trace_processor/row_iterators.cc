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

#include "src/trace_processor/row_iterators.h"

#include <memory>

#include "perfetto/base/logging.h"
#include "src/trace_processor/sqlite_utils.h"
#include "src/trace_processor/table.h"

namespace perfetto {
namespace trace_processor {

namespace {

template <typename Iterator>
uint32_t FindNextOffset(Iterator begin, Iterator end, uint32_t offset) {
  auto prev_it = begin + static_cast<ptrdiff_t>(offset);
  auto current_it = std::find(prev_it, end, true);
  return static_cast<uint32_t>(std::distance(begin, current_it));
}

}  // namespace

FilteredRowIterator::FilteredRowIterator(uint32_t start_row,
                                         uint32_t end_row,
                                         bool desc)
    : start_row_(start_row), end_row_(end_row), desc_(desc) {}

FilteredRowIterator::FilteredRowIterator(uint32_t start_row,
                                         bool desc,
                                         std::vector<bool> row_filter)
    : start_row_(start_row),
      end_row_(start_row_ + static_cast<uint32_t>(row_filter.size())),
      desc_(desc),
      row_filter_(std::move(row_filter)) {
  if (start_row_ < end_row_)
    NextRow();
}

void FilteredRowIterator::NextRow() {
  PERFETTO_DCHECK(!IsEnd());
  offset_++;
  if (row_filter_.empty())
    return;

  if (desc_)
    offset_ = FindNextOffset(row_filter_.rbegin(), row_filter_.rend(), offset_);
  else
    offset_ = FindNextOffset(row_filter_.begin(), row_filter_.end(), offset_);
}

SortedRowIterator::SortedRowIterator(std::vector<uint32_t> sorted_rows)
    : sorted_rows_(std::move(sorted_rows)) {}
SortedRowIterator::~SortedRowIterator() = default;

std::unique_ptr<StorageCursor::RowIterator> CreateOptimalRowIterator(
    const Table::Schema& schema,
    const StorageCursor::ValueRetriever& retr,
    int natural_bounding_column,
    std::pair<uint32_t, uint32_t> natural_bounding_indices,
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  auto min_idx = natural_bounding_indices.first;
  auto max_idx = natural_bounding_indices.second;
  bool desc = qc.order_by().size() == 1 && qc.order_by()[0].desc;

  FilteredRowIterator inner_it(min_idx, max_idx, desc);
  if (!sqlite_utils::HasOnlyConstraintsForColumn(qc, natural_bounding_column)) {
    std::vector<bool> filter(max_idx - min_idx);
    const auto& cs = qc.constraints();
    for (size_t i = 0; i < cs.size(); i++) {
      sqlite_utils::FilterOnConstraint(schema, retr, cs[i], argv[i], min_idx,
                                       &filter);
    }
    inner_it = FilteredRowIterator(min_idx, desc, std::move(filter));
  }

  if (sqlite_utils::IsNaturallyOrdered(qc, natural_bounding_column))
    return base::make_unique<FilteredRowIterator>(std::move(inner_it));

  std::vector<uint32_t> sorted_rows(inner_it.RowCount());
  for (size_t i = 0; !inner_it.IsEnd(); inner_it.NextRow(), i++)
    sorted_rows[i] = inner_it.Row();
  sqlite_utils::SortOnOrderBys(schema, retr, qc.order_by(), &sorted_rows);

  return base::make_unique<SortedRowIterator>(std::move(sorted_rows));
}

}  // namespace trace_processor
}  // namespace perfetto
