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

#ifndef SRC_TRACE_PROCESSOR_STORAGE_TABLE_H_
#define SRC_TRACE_PROCESSOR_STORAGE_TABLE_H_

#include <deque>
#include <set>

#include "src/trace_processor/filtered_row_index.h"
#include "src/trace_processor/row_iterators.h"
#include "src/trace_processor/table.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class StorageTable : public Table {
 public:
  // A column of data backed by data storage.
  class Column {
   public:
    struct Bounds {
      uint32_t min_idx = 0;
      uint32_t max_idx = std::numeric_limits<uint32_t>::max();
      bool consumed = false;
    };
    using Predicate = std::function<bool(uint32_t)>;
    using Comparator = std::function<int(uint32_t, uint32_t)>;

    Column(std::string col_name, bool hidden);
    virtual ~Column();

    // Implements StorageCursor::ColumnReporter.
    virtual void ReportResult(sqlite3_context*, uint32_t) const = 0;

    // Bounds a filter on this column between a minimum and maximum index.
    // Generally this is only possible if the column is sorted.
    virtual Bounds BoundFilter(int op, sqlite3_value* value) const = 0;

    // Given a SQLite operator and value for the comparision, returns a
    // predicate which takes in a row index and returns whether the row should
    // be returned.
    virtual void Filter(int op, sqlite3_value*, FilteredRowIndex*) const = 0;

    // Given a order by constraint for this column, returns a comparator
    // function which compares data in this column at two indices.
    virtual Comparator Sort(const QueryConstraints::OrderBy& ob) const = 0;

    // Returns the type of this column.
    virtual Table::ColumnType GetType() const = 0;

    // Returns whether this column is sorted in the storage.
    virtual bool IsNaturallyOrdered() const = 0;

    const std::string& name() const { return col_name_; }
    bool hidden() const { return hidden_; }

   private:
    std::string col_name_;
    bool hidden_ = false;
  };

  class Cursor : Table::Cursor {
    Cursor(std::unique_ptr<RowIterator>,
           std::vector<std::unique_ptr<StorageTable::Column>>*);

    // Implementation of Table::Cursor.
    int Next() override;
    int Eof() override;
    int Column(sqlite3_context*, int N) override;

   private:
    std::unique_ptr<RowIterator> iterator_;
    std::vector<std::unique_ptr<StorageTable::Column>>* columns_;
  };

  // A column of numeric data backed by a deque.
  template <typename T>
  class NumericColumn final : public Column {
   public:
    NumericColumn(std::string col_name,
                  const std::deque<T>* deque,
                  bool hidden,
                  bool is_naturally_ordered)
        : Column(col_name, hidden),
          deque_(deque),
          is_naturally_ordered_(is_naturally_ordered) {}

    void ReportResult(sqlite3_context* ctx, uint32_t row) const override {
      sqlite_utils::ReportSqliteResult(ctx, (*deque_)[row]);
    }

    Bounds BoundFilter(int op, sqlite3_value* sqlite_val) const override {
      Bounds bounds;
      bounds.max_idx = static_cast<uint32_t>(deque_->size());

      if (!is_naturally_ordered_)
        return bounds;

      // Makes the below code much more readable.
      using namespace sqlite_utils;

      T min = kTMin;
      T max = kTMax;
      if (IsOpGe(op) || IsOpGt(op)) {
        min = FindGtBound<T>(IsOpGe(op), sqlite_val);
      } else if (IsOpLe(op) || IsOpLt(op)) {
        max = FindLtBound<T>(IsOpLe(op), sqlite_val);
      } else if (IsOpEq(op)) {
        auto val = FindEqBound<T>(sqlite_val);
        min = val;
        max = val;
      }

      if (min <= kTMin && max >= kTMax)
        return bounds;

      // Convert the values into indices into the deque.
      auto min_it = std::lower_bound(deque_->begin(), deque_->end(), min);
      bounds.min_idx =
          static_cast<uint32_t>(std::distance(deque_->begin(), min_it));
      auto max_it = std::upper_bound(min_it, deque_->end(), max);
      bounds.max_idx =
          static_cast<uint32_t>(std::distance(deque_->begin(), max_it));
      bounds.consumed = true;

      return bounds;
    }

    void Filter(int op,
                sqlite3_value* value,
                FilteredRowIndex* index) const override {
      auto type = sqlite3_value_type(value);
      if (type == SQLITE_INTEGER && std::is_integral<T>::value) {
        FilterWithCast<int64_t>(op, value, index);
      } else if (type == SQLITE_INTEGER || type == SQLITE_FLOAT) {
        FilterWithCast<double>(op, value, index);
      } else {
        PERFETTO_FATAL("Unexpected sqlite value to compare against");
      }
    }

    Comparator Sort(const QueryConstraints::OrderBy& ob) const override {
      if (ob.desc) {
        return [this](uint32_t f, uint32_t s) {
          return sqlite_utils::CompareValuesDesc((*deque_)[f], (*deque_)[s]);
        };
      }
      return [this](uint32_t f, uint32_t s) {
        return sqlite_utils::CompareValuesAsc((*deque_)[f], (*deque_)[s]);
      };
    }

    bool IsNaturallyOrdered() const override { return is_naturally_ordered_; }

    Table::ColumnType GetType() const override {
      if (std::is_same<T, int32_t>::value) {
        return Table::ColumnType::kInt;
      } else if (std::is_same<T, uint8_t>::value ||
                 std::is_same<T, uint32_t>::value) {
        return Table::ColumnType::kUint;
      } else if (std::is_same<T, int64_t>::value) {
        return Table::ColumnType::kLong;
      } else if (std::is_same<T, uint64_t>::value) {
        return Table::ColumnType::kUlong;
      } else if (std::is_same<T, double>::value) {
        return Table::ColumnType::kDouble;
      }
      PERFETTO_CHECK(false);
    }

   private:
    T kTMin = std::numeric_limits<T>::lowest();
    T kTMax = std::numeric_limits<T>::max();

    template <typename C>
    void FilterWithCast(int op,
                        sqlite3_value* value,
                        FilteredRowIndex* index) const {
      auto binary_op = sqlite_utils::GetPredicateForOp<C>(op);
      C extracted = sqlite_utils::ExtractSqliteValue<C>(value);
      index->FilterRows([this, binary_op, extracted](uint32_t row) {
        auto val = static_cast<C>((*deque_)[row]);
        return binary_op(val, extracted);
      });
    }

    const std::deque<T>* deque_ = nullptr;
    bool is_naturally_ordered_ = false;
  };

  template <typename Id>
  class StringColumn final : public Column {
   public:
    StringColumn(std::string col_name,
                 const std::deque<Id>* deque,
                 const std::deque<std::string>* string_map,
                 bool hidden = false)
        : Column(col_name, hidden), deque_(deque), string_map_(string_map) {}

    void ReportResult(sqlite3_context* ctx, uint32_t row) const override {
      const auto& str = (*string_map_)[(*deque_)[row]];
      if (str.empty()) {
        sqlite3_result_null(ctx);
      } else {
        auto kStatic = static_cast<sqlite3_destructor_type>(0);
        sqlite3_result_text(ctx, str.c_str(), -1, kStatic);
      }
    }

    Bounds BoundFilter(int, sqlite3_value*) const override {
      Bounds bounds;
      bounds.max_idx = static_cast<uint32_t>(deque_->size());
      return bounds;
    }

    void Filter(int, sqlite3_value*, FilteredRowIndex*) const override {}

    Comparator Sort(const QueryConstraints::OrderBy& ob) const override {
      if (ob.desc) {
        return [this](uint32_t f, uint32_t s) {
          const std::string& a = (*string_map_)[(*deque_)[f]];
          const std::string& b = (*string_map_)[(*deque_)[s]];
          return sqlite_utils::CompareValuesDesc(a, b);
        };
      }
      return [this](uint32_t f, uint32_t s) {
        const std::string& a = (*string_map_)[(*deque_)[f]];
        const std::string& b = (*string_map_)[(*deque_)[s]];
        return sqlite_utils::CompareValuesAsc(a, b);
      };
    }

    Table::ColumnType GetType() const override {
      return Table::ColumnType::kString;
    }

    bool IsNaturallyOrdered() const override { return false; }

   private:
    const std::deque<Id>* deque_ = nullptr;
    const std::deque<std::string>* string_map_ = nullptr;
  };

  // Column which represents the "ts_end" column present in all time based
  // tables. It is computed by adding together the values in two deques.
  class TsEndColumn final : public Column {
   public:
    TsEndColumn(std::string col_name,
                const std::deque<uint64_t>* ts_start,
                const std::deque<uint64_t>* dur);
    virtual ~TsEndColumn() override;

    void ReportResult(sqlite3_context*, uint32_t) const override;

    Bounds BoundFilter(int op, sqlite3_value* value) const override;

    void Filter(int op, sqlite3_value* value, FilteredRowIndex*) const override;

    Comparator Sort(const QueryConstraints::OrderBy& ob) const override;

    // Returns the type of this column.
    Table::ColumnType GetType() const override {
      return Table::ColumnType::kUlong;
    }

    bool IsNaturallyOrdered() const override { return false; }

   private:
    const std::deque<uint64_t>* ts_start_;
    const std::deque<uint64_t>* dur_;
  };

  // Column which is used to reference the args table in other tables. That is,
  // it acts as a "foreign key" into the args table.
  class IdColumn final : public Column {
   public:
    IdColumn(std::string column_name, TableId table_id);
    virtual ~IdColumn() override;

    void ReportResult(sqlite3_context* ctx, uint32_t row) const override {
      auto id = TraceStorage::CreateRowId(table_id_, row);
      sqlite_utils::ReportSqliteResult(ctx, id);
    }

    Bounds BoundFilter(int, sqlite3_value*) const override { return Bounds{}; }

    void Filter(int op,
                sqlite3_value* value,
                FilteredRowIndex* index) const override {
      auto binary_op = sqlite_utils::GetPredicateForOp<RowId>(op);
      RowId extracted = sqlite_utils::ExtractSqliteValue<RowId>(value);
      index->FilterRows([this, &binary_op, extracted](uint32_t row) {
        auto val = TraceStorage::CreateRowId(table_id_, row);
        return binary_op(val, extracted);
      });
    }

    Comparator Sort(const QueryConstraints::OrderBy& ob) const override {
      if (ob.desc) {
        return [this](uint32_t f, uint32_t s) {
          auto a = TraceStorage::CreateRowId(table_id_, f);
          auto b = TraceStorage::CreateRowId(table_id_, s);
          return sqlite_utils::CompareValuesDesc(a, b);
        };
      }
      return [this](uint32_t f, uint32_t s) {
        auto a = TraceStorage::CreateRowId(table_id_, f);
        auto b = TraceStorage::CreateRowId(table_id_, s);
        return sqlite_utils::CompareValuesAsc(a, b);
      };
    }

    Table::ColumnType GetType() const override {
      return Table::ColumnType::kUlong;
    }

    bool IsNaturallyOrdered() const override { return false; }

   private:
    TableId table_id_;
  };

  template <typename T>
  static std::unique_ptr<TsEndColumn> TsEndPtr(std::string column_name,
                                               const std::deque<T>* ts_start,
                                               const std::deque<T>* ts_end) {
    return std::unique_ptr<TsEndColumn>(
        new TsEndColumn(column_name, ts_start, ts_end));
  }

  template <typename T>
  static std::unique_ptr<NumericColumn<T>> NumericColumnPtr(
      std::string column_name,
      const std::deque<T>* deque,
      bool hidden = false,
      bool is_naturally_ordered = false) {
    return std::unique_ptr<NumericColumn<T>>(
        new NumericColumn<T>(column_name, deque, hidden, is_naturally_ordered));
  }

  template <typename Id>
  static std::unique_ptr<StringColumn<Id>> StringColumnPtr(
      std::string column_name,
      const std::deque<Id>* deque,
      const std::deque<std::string>* lookup_map,
      bool hidden = false) {
    return std::unique_ptr<StringColumn<Id>>(
        new StringColumn<Id>(column_name, deque, lookup_map, hidden));
  }

  static std::unique_ptr<IdColumn> IdColumnPtr(std::string column_name,
                                               TableId table_id) {
    return std::unique_ptr<IdColumn>(new IdColumn(column_name, table_id));
  }

  StorageTable(std::vector<std::unique_ptr<Column>> columns);

 private:
  inline RangeRowIterator CreateRangeIterator(
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
      auto bounds = GetColumn(column).BoundFilter(c.op, argv[i]);

      min_idx = std::max(min_idx, bounds.min_idx);
      max_idx = std::min(max_idx, bounds.max_idx);

      // If the lower bound is higher than the upper bound, return a zero-sized
      // range iterator.
      if (min_idx >= max_idx)
        return RangeRowIterator(min_idx, min_idx, desc);

      if (!bounds.consumed)
        bitvector_cs.emplace_back(i);
    }

    // Create an filter index and allow each of the columns filter on it.
    FilteredRowIndex index(min_idx, max_idx);
    for (const auto& c_idx : bitvector_cs) {
      const auto& c = cs[c_idx];
      auto* value = argv[c_idx];

      const auto& schema_col = GetColumn(static_cast<size_t>(c.iColumn));
      schema_col.Filter(c.op, value, &index);
    }

    switch (index.mode()) {
      case FilteredRowIndex::Mode::kAllRows:
        return RangeRowIterator(min_idx, max_idx, desc);
      case FilteredRowIndex::Mode::kBitVector:
        return RangeRowIterator(min_idx, desc, index.ReleaseBitVector());
    }
    PERFETTO_CHECK(false);
  }

  inline std::pair<bool, bool> IsOrdered(
      const std::vector<QueryConstraints::OrderBy>& obs) {
    if (obs.size() == 0)
      return std::make_pair(true, false);

    if (obs.size() != 1)
      return std::make_pair(false, false);

    const auto& ob = obs[0];
    auto col = static_cast<size_t>(ob.iColumn);
    return std::make_pair(GetColumn(col).IsNaturallyOrdered(), ob.desc);
  }

  inline std::vector<QueryConstraints::OrderBy> RemoveRedundantOrderBy(
      const std::vector<QueryConstraints::Constraint>& cs,
      const std::vector<QueryConstraints::OrderBy>& obs) {
    std::vector<QueryConstraints::OrderBy> filtered;
    std::set<int> equality_cols;
    for (const auto& c : cs) {
      if (sqlite_utils::IsOpEq(c.op))
        equality_cols.emplace(c.iColumn);
    }
    for (const auto& o : obs) {
      if (equality_cols.count(o.iColumn) > 0)
        continue;
      filtered.emplace_back(o);
    }
    return filtered;
  }

  inline std::vector<uint32_t> CreateSortedIndexVector(
      RangeRowIterator it,
      const std::vector<QueryConstraints::OrderBy>& obs) {
    PERFETTO_DCHECK(obs.size() > 0);

    std::vector<uint32_t> sorted_rows(it.RowCount());
    for (size_t i = 0; !it.IsEnd(); it.NextRow(), i++)
      sorted_rows[i] = it.Row();

    std::vector<Column::Comparator> comparators;
    for (const auto& ob : obs) {
      auto col = static_cast<size_t>(ob.iColumn);
      comparators.emplace_back(GetColumn(col).Sort(ob));
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

  Table::Schema ToTableSchema(std::vector<std::string> primary_keys);

  size_t ColumnIndexFromName(const std::string& name);

  const Column& GetColumn(size_t idx) const { return *(columns_[idx]); }

  std::vector<std::unique_ptr<Column>> columns_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_STORAGE_TABLE_H_
