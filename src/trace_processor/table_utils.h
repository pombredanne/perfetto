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

#ifndef SRC_TRACE_PROCESSOR_TABLE_UTILS_H_
#define SRC_TRACE_PROCESSOR_TABLE_UTILS_H_

#include <memory>

#include "src/trace_processor/row_iterators.h"
#include "src/trace_processor/storage_cursor.h"

namespace perfetto {
namespace trace_processor {
namespace table_utils {

namespace internal {

inline FilteredRowIterator CreateFilteredIterator(
    const std::vector<std::unique_ptr<StorageCursor::ColumnDefn>>& cols,
    uint32_t size,
    bool desc,
    const std::vector<QueryConstraints::Constraint>& cs,
    sqlite3_value** argv) {
  // Try and bound the search space to the smallest possible index region and
  // store any leftover constraints to filter using bitvector.
  uint32_t min_idx = 0;
  uint32_t max_idx = size;
  std::vector<size_t> bitvector_cs;
  for (size_t i = 0; i < cs.size(); i++) {
    const auto& c = cs[i];
    size_t column = static_cast<size_t>(c.iColumn);
    auto bounds = cols[column]->BoundFilter(c.op, argv[i]);
    if (bounds.consumed) {
      min_idx = std::max(min_idx, bounds.min_idx);
      max_idx = std::min(max_idx, bounds.max_idx);
    } else {
      bitvector_cs.emplace_back(i);
    }
  }

  // If we have no other constraints then we can just iterate between min
  // and max.
  if (bitvector_cs.empty())
    return FilteredRowIterator(min_idx, max_idx, desc);

  // Otherwise, create a bitvector with true meaning the row should be returned
  // and false otherwise.
  std::vector<bool> filter(max_idx - min_idx, true);
  for (const auto& c_idx : bitvector_cs) {
    const auto& c = cs[c_idx];
    auto* value = argv[c_idx];

    auto col = static_cast<size_t>(c.iColumn);
    auto predicate = cols[col]->Filter(c.op, value);

    auto b = filter.begin();
    auto e = filter.end();
    using std::find;
    for (auto it = find(b, e, true); it != e; it = find(it + 1, e, true)) {
      auto filter_idx = static_cast<uint32_t>(std::distance(b, it));
      *it = predicate(min_idx + filter_idx);
    }
  }
  return FilteredRowIterator(min_idx, desc, std::move(filter));
}

inline std::pair<bool, bool> IsOrdered(
    const std::vector<std::unique_ptr<StorageCursor::ColumnDefn>>& cols,
    const std::vector<QueryConstraints::OrderBy>& obs) {
  if (obs.size() != 1)
    return std::make_pair(false, false);

  const auto& ob = obs[0];
  auto col = static_cast<size_t>(ob.iColumn);
  return std::make_pair(cols[col]->IsNaturallyOrdered(), ob.desc);
}

inline std::vector<uint32_t> CreateSortedIndexVector(
    const std::vector<std::unique_ptr<StorageCursor::ColumnDefn>>& cols,
    FilteredRowIterator it,
    const std::vector<QueryConstraints::OrderBy>& obs) {
  std::vector<uint32_t> sorted_rows(it.RowCount());
  for (size_t i = 0; !it.IsEnd(); it.NextRow(), i++)
    sorted_rows[i] = it.Row();

  std::vector<StorageCursor::ColumnDefn::Comparator> comparators;
  for (const auto& ob : obs) {
    auto col = static_cast<size_t>(ob.iColumn);
    comparators.emplace_back(cols[col]->Sort(ob));
  }

  auto comparator = [&comparators](uint32_t f, uint32_t s) {
    for (const auto& comp : comparators) {
      int c = comp(f, s);
      if (c != 0)
        return c < 0;
    }
    return false;
  };
  std::sort(sorted_rows.begin(), sorted_rows.end(), comparator);

  return sorted_rows;
}

}  // namespace internal

inline size_t ColumnIndexFromName(
    const std::vector<std::unique_ptr<StorageCursor::ColumnDefn>>& cols,
    const std::string& name) {
  auto p = [name](const std::unique_ptr<StorageCursor::ColumnDefn>& col) {
    return name == col->name();
  };
  auto it = std::find_if(cols.begin(), cols.end(), p);
  return static_cast<size_t>(std::distance(cols.begin(), it));
}

inline Table::Schema CreateSchemaFromStorageColumns(
    const std::vector<std::unique_ptr<StorageCursor::ColumnDefn>>& cols,
    const std::vector<std::string>& p_key_names) {
  std::vector<Table::Column> columns;
  size_t i = 0;
  for (const auto& col : cols)
    columns.emplace_back(i++, col->name(), col->GetType(), col->hidden());

  std::vector<size_t> primary_keys;
  for (const auto& p_key : p_key_names)
    primary_keys.emplace_back(ColumnIndexFromName(cols, p_key));
  return Table::Schema(std::move(columns), std::move(primary_keys));
}

inline std::unique_ptr<StorageCursor::RowIterator> CreateOptimalRowIterator(
    const std::vector<std::unique_ptr<StorageCursor::ColumnDefn>>& cols,
    uint32_t size,
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  const auto& cs = qc.constraints();
  const auto& obs = qc.order_by();

  // Figure out whether the data is already ordered and which order we should
  // traverse the data.
  bool is_ordered, desc = false;
  std::tie(is_ordered, desc) = internal::IsOrdered(cols, obs);

  // Create the filter iterator and if we are sorted, just return it.
  auto filter_it = internal::CreateFilteredIterator(cols, size, desc, cs, argv);
  if (is_ordered)
    return std::unique_ptr<FilteredRowIterator>(
        new FilteredRowIterator(std::move(filter_it)));

  // Otherwise, create the sorted vector of indices and create the sorted
  // iterator.
  return std::unique_ptr<SortedRowIterator>(new SortedRowIterator(
      internal::CreateSortedIndexVector(cols, std::move(filter_it), obs)));
}

}  // namespace table_utils
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TABLE_UTILS_H_
