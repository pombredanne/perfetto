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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_UTILS_H_
#define SRC_TRACE_PROCESSOR_SQLITE_UTILS_H_

#include <sqlite3.h>
#include <algorithm>
#include <deque>
#include <iterator>

#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"
#include "src/trace_processor/query_constraints.h"
#include "src/trace_processor/storage_cursor.h"

namespace perfetto {
namespace trace_processor {
namespace sqlite_utils {

namespace internal {

constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();

template <typename T>
T ExtractSqliteValue(sqlite3_value* value);

template <>
inline uint32_t ExtractSqliteValue(sqlite3_value* value) {
  PERFETTO_DCHECK(sqlite3_value_type(value) == SQLITE_INTEGER);
  return static_cast<uint32_t>(sqlite3_value_int64(value));
}

template <>
inline uint64_t ExtractSqliteValue(sqlite3_value* value) {
  PERFETTO_DCHECK(sqlite3_value_type(value) == SQLITE_INTEGER);
  return static_cast<uint64_t>(sqlite3_value_int64(value));
}

template <>
inline int64_t ExtractSqliteValue(sqlite3_value* value) {
  PERFETTO_DCHECK(sqlite3_value_type(value) == SQLITE_INTEGER);
  return static_cast<int64_t>(sqlite3_value_int64(value));
}

template <>
inline double ExtractSqliteValue(sqlite3_value* value) {
  auto type = sqlite3_value_type(value);
  PERFETTO_DCHECK(type == SQLITE_FLOAT || type == SQLITE_INTEGER);
  return sqlite3_value_double(value);
}

// Generic filtering support for tables.
template <class T>
std::function<bool(T, T)> GetPredicateForOp(int op) {
  switch (op) {
    case SQLITE_INDEX_CONSTRAINT_EQ:
      return std::equal_to<T>();
    case SQLITE_INDEX_CONSTRAINT_GE:
      return std::greater_equal<T>();
    case SQLITE_INDEX_CONSTRAINT_GT:
      return std::greater<T>();
    case SQLITE_INDEX_CONSTRAINT_LE:
      return std::less_equal<T>();
    case SQLITE_INDEX_CONSTRAINT_LT:
      return std::less<T>();
    case SQLITE_INDEX_CONSTRAINT_NE:
      return std::not_equal_to<T>();
    default:
      PERFETTO_CHECK(false);
  }
}

template <typename Retriever>
void FilterOnColumn(const Retriever& retriver,
                    int op,
                    sqlite3_value* value,
                    std::vector<bool>* filter) {
  using T = decltype(retriver(0));
  auto predicate = GetPredicateForOp<T>(op);

  auto it = std::find(filter->begin(), filter->end(), true);
  while (it != filter->end()) {
    auto filter_idx = static_cast<uint32_t>(std::distance(filter->begin(), it));
    T actual = retriver(filter_idx);
    T extracted = ExtractSqliteValue<T>(value);
    *it = predicate(actual, extracted);
    it = std::find(it + 1, filter->end(), true);
  }
}

// Generic sort support for tables.
template <typename Retriever>
int CompareValues(const Retriever& retriver, size_t a, size_t b, bool desc) {
  const auto& first = retriver(static_cast<uint32_t>(a));
  const auto& second = retriver(static_cast<uint32_t>(b));
  if (first < second) {
    return desc ? 1 : -1;
  } else if (first > second) {
    return desc ? -1 : 1;
  }
  return 0;
}

inline int CompareOnColumn(const Table::Schema& schema,
                           const StorageCursor::ValueRetriever& retr,
                           uint32_t f_idx,
                           uint32_t s_idx,
                           const QueryConstraints::OrderBy& ob) {
  size_t col = static_cast<size_t>(ob.iColumn);
  switch (schema.columns()[col].type()) {
    case Table::ColumnType::kUint: {
      auto ret = [col, &retr](uint32_t idx) { return retr.GetUint(col, idx); };
      return CompareValues(ret, f_idx, s_idx, ob.desc);
    }
    case Table::ColumnType::kUlong: {
      auto ret = [col, &retr](uint32_t idx) { return retr.GetUlong(col, idx); };
      return CompareValues(ret, f_idx, s_idx, ob.desc);
    }
    case Table::ColumnType::kDouble: {
      auto ret = [col, &retr](uint32_t idx) {
        return retr.GetDouble(col, idx);
      };
      return CompareValues(ret, f_idx, s_idx, ob.desc);
    }
    case Table::ColumnType::kLong: {
      auto ret = [col, &retr](uint32_t idx) { return retr.GetLong(col, idx); };
      return CompareValues(ret, f_idx, s_idx, ob.desc);
    }
    case Table::ColumnType::kInt:
    case Table::ColumnType::kString:
      PERFETTO_CHECK(false);
  }
}

}  // namespace internal

inline bool IsOpEq(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_EQ;
}

inline bool IsOpGe(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_GE;
}

inline bool IsOpGt(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_GT;
}

inline bool IsOpLe(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_LE;
}

inline bool IsOpLt(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_LT;
}

inline std::string OpToString(int op) {
  switch (op) {
    case SQLITE_INDEX_CONSTRAINT_EQ:
      return "=";
    case SQLITE_INDEX_CONSTRAINT_NE:
      return "!=";
    case SQLITE_INDEX_CONSTRAINT_GE:
      return ">=";
    case SQLITE_INDEX_CONSTRAINT_GT:
      return ">";
    case SQLITE_INDEX_CONSTRAINT_LE:
      return "<=";
    case SQLITE_INDEX_CONSTRAINT_LT:
      return "<";
    default:
      PERFETTO_FATAL("Operator to string conversion not impemented for %d", op);
  }
}

inline bool IsNaturallyOrdered(const QueryConstraints& qc,
                               int natural_ordered_column) {
  return qc.order_by().size() == 0 ||
         (qc.order_by().size() == 1 &&
          qc.order_by()[0].iColumn == natural_ordered_column);
}

inline bool HasOnlyConstraintsForColumn(const QueryConstraints& qc,
                                        int column) {
  auto fn = [column](const QueryConstraints::Constraint& c) {
    return c.iColumn == column;
  };
  return std::all_of(qc.constraints().begin(), qc.constraints().end(), fn);
}

template <typename T>
std::pair<T, T> GetBoundsForNumericColumn(const QueryConstraints& qc,
                                          sqlite3_value** argv,
                                          int column) {
  uint64_t min = 0;
  uint64_t max = internal::kUint64Max;
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    const auto& cs = qc.constraints()[i];
    if (cs.iColumn != column)
      continue;

    auto value = internal::ExtractSqliteValue<T>(argv[i]);
    if (IsOpGe(cs.op) || IsOpGt(cs.op)) {
      min = IsOpGe(cs.op) ? value : value + 1;
    } else if (IsOpLe(cs.op) || IsOpLt(cs.op)) {
      max = IsOpLe(cs.op) ? value : value - 1;
    } else if (IsOpEq(cs.op)) {
      min = value;
      max = value;
    } else {
      // We can't handle any other constraints on ts.
      PERFETTO_CHECK(false);
    }
  }
  return std::make_pair(min, max);
}

inline void FilterOnConstraint(const Table::Schema& schema,
                               const StorageCursor::ValueRetriever& retr,
                               const QueryConstraints::Constraint& cs,
                               sqlite3_value* value,
                               uint32_t offset,
                               std::vector<bool>* filter) {
  size_t col = static_cast<size_t>(cs.iColumn);
  switch (schema.columns()[col].type()) {
    case Table::ColumnType::kUint: {
      auto ret = [col, &retr, offset](uint32_t idx) {
        return retr.GetUint(col, offset + idx);
      };
      internal::FilterOnColumn(ret, cs.op, value, filter);
      break;
    }
    case Table::ColumnType::kUlong: {
      auto ret = [col, &retr, offset](uint32_t idx) {
        return retr.GetUlong(col, offset + idx);
      };
      internal::FilterOnColumn(ret, cs.op, value, filter);
      break;
    }
    case Table::ColumnType::kDouble: {
      auto ret = [col, &retr, offset](uint32_t idx) {
        return retr.GetDouble(col, offset + idx);
      };
      internal::FilterOnColumn(ret, cs.op, value, filter);
      break;
    }
    case Table::ColumnType::kLong: {
      auto ret = [col, &retr, offset](uint32_t idx) {
        return retr.GetLong(col, offset + idx);
      };
      internal::FilterOnColumn(ret, cs.op, value, filter);
      break;
    }
    case Table::ColumnType::kInt:
    case Table::ColumnType::kString:
      PERFETTO_CHECK(false);
  }
}

inline void SortOnOrderBys(const Table::Schema& schema,
                           const StorageCursor::ValueRetriever& retr,
                           const std::vector<QueryConstraints::OrderBy> obs,
                           std::vector<uint32_t>* idxs) {
  std::vector<std::function<int(uint32_t, uint32_t)>> comparators;
  for (const auto& ob : obs) {
    comparators.emplace_back([&schema, &retr, &ob](uint32_t f, uint32_t s) {
      return internal::CompareOnColumn(schema, retr, f, s, ob);
    });
  }
  std::sort(idxs->begin(), idxs->end(), [comparators](uint32_t f, uint32_t s) {
    for (const auto& comp : comparators) {
      int c = comp(f, s);
      if (c != 0)
        return c < 0;
    }
    return false;
  });
}

}  // namespace sqlite_utils
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_SQLITE_UTILS_H_
