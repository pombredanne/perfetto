/*
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

#ifndef SRC_TRACE_PROCESSOR_STORAGE_SCHEMA_H_
#define SRC_TRACE_PROCESSOR_STORAGE_SCHEMA_H_

#include <deque>

#include "src/trace_processor/sqlite_utils.h"
#include "src/trace_processor/table.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class StorageSchema {
 public:
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

    virtual Bounds BoundFilter(int op, sqlite3_value* value) const = 0;
    virtual Predicate Filter(int op, sqlite3_value* value) const = 0;
    virtual Comparator Sort(QueryConstraints::OrderBy ob) const = 0;
    virtual void ReportResult(sqlite3_context*, uint32_t row) const = 0;
    virtual Table::ColumnType GetType() const = 0;
    virtual bool IsNaturallyOrdered() const = 0;

    const std::string& name() const { return col_name_; }
    bool hidden() const { return hidden_; }

   private:
    std::string col_name_;
    bool hidden_ = false;
  };

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

    Bounds BoundFilter(int op, sqlite3_value* sqlite_val) const override {
      Bounds bounds;
      bounds.max_idx = static_cast<uint32_t>(deque_->size());

      if (!is_naturally_ordered_)
        return bounds;

      auto min = std::numeric_limits<T>::min();
      auto max = std::numeric_limits<T>::max();

      // Makes the below code much more readable.
      using namespace sqlite_utils;

      // Try and bound the min and max value based on the constraints.
      auto value = sqlite_utils::ExtractSqliteValue<T>(sqlite_val);
      if (IsOpGe(op) || IsOpGt(op)) {
        min = IsOpGe(op) ? value : value + 1;
      } else if (IsOpLe(op) || IsOpLt(op)) {
        max = IsOpLe(op) ? value : value - 1;
      } else if (IsOpEq(op)) {
        min = value;
        max = value;
      } else {
        // We cannot bound on this constraint.
        return bounds;
      }

      // Convert the values into indices into the deque.
      auto min_it = std::lower_bound(deque_->begin(), deque_->end(), min);
      bounds.min_idx =
          static_cast<uint32_t>(std::distance(deque_->begin(), min_it));
      auto max_it = std::upper_bound(min_it, deque_->end(), max);
      bounds.max_idx =
          static_cast<uint32_t>(std::distance(deque_->begin(), max_it));

      return bounds;
    }

    Predicate Filter(int op, sqlite3_value* value) const override {
      auto bipredicate = sqlite_utils::GetPredicateForOp<T>(op);
      T extracted = sqlite_utils::ExtractSqliteValue<T>(value);
      return [this, bipredicate, extracted](uint32_t idx) {
        return bipredicate(deque_->operator[](idx), extracted);
      };
    }

    Comparator Sort(QueryConstraints::OrderBy ob) const override {
      if (ob.desc) {
        return [this](uint32_t f, uint32_t s) {
          auto a = deque_[f];
          auto b = deque_[s];
          if (a < b)
            return -1;
          else if (a > b)
            return 1;
          return 0;
        };
      }
      return [this](uint32_t f, uint32_t s) {
        auto a = deque_[f];
        auto b = deque_[s];
        if (a > b)
          return -1;
        else if (a < b)
          return 1;
        return 0;
      };
    }

    void ReportResult(sqlite3_context* ctx, uint32_t row) const override {
      sqlite_utils::ReportSqliteResult(ctx, deque_->operator[](row));
    }

    bool IsNaturallyOrdered() const override { return is_naturally_ordered_; }

    Table::ColumnType GetType() const override {
      if (std::is_same<T, int32_t>::value) {
        return Table::ColumnType::kInt;
      } else if (std::is_same<T, uint32_t>::value) {
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

    Bounds BoundFilter(int, sqlite3_value*) const override {
      Bounds bounds;
      bounds.max_idx = static_cast<uint32_t>(deque_->size());
      return bounds;
    }

    Predicate Filter(int, sqlite3_value*) const override {
      return [](uint32_t) { return true; };
    }

    Comparator Sort(QueryConstraints::OrderBy ob) const override {
      if (ob.desc) {
        return [this](uint32_t f, uint32_t s) {
          const auto& a = string_map_->operator[](deque_->operator[](f));
          const auto& b = string_map_->operator[](deque_->operator[](s));
          if (a < b)
            return -1;
          else if (a > b)
            return 1;
          return 0;
        };
      }
      return [this](uint32_t f, uint32_t s) {
        const auto& a = string_map_->operator[](deque_->operator[](f));
        const auto& b = string_map_->operator[](deque_->operator[](s));
        if (a > b)
          return -1;
        else if (a < b)
          return 1;
        return 0;
      };
    }

    void ReportResult(sqlite3_context* ctx, uint32_t row) const override {
      const auto& str = string_map_->operator[](deque_->operator[](row));
      if (str.empty()) {
        sqlite3_result_null(ctx);
      } else {
        auto kStatic = static_cast<sqlite3_destructor_type>(0);
        sqlite3_result_text(ctx, str.c_str(), -1, kStatic);
      }
    }

    Table::ColumnType GetType() const override {
      return Table::ColumnType::kString;
    }

    bool IsNaturallyOrdered() const override { return false; }

   private:
    const std::deque<Id>* deque_ = nullptr;
    const std::deque<std::string>* string_map_ = nullptr;
  };

  StorageSchema();
  StorageSchema(std::vector<std::unique_ptr<Column>> columns);

  Table::Schema ToTableSchema(std::vector<std::string> primary_keys);

  size_t ColumnIndexFromName(const std::string& name);

  std::vector<const Column*> Columns() const {
    std::vector<const Column*> defns;
    for (const auto& col : columns_)
      defns.emplace_back(col.get());
    return defns;
  }

  const Column& GetColumn(size_t idx) const { return *(columns_[idx]); }

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

 private:
  std::vector<std::unique_ptr<Column>> columns_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_STORAGE_SCHEMA_H_
