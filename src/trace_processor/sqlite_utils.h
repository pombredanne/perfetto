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

namespace perfetto {
namespace trace_processor {
namespace sqlite_utils {

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

template <typename T,
          typename = typename std::
              enable_if<std::is_convertible<sqlite3_int64, T>::value, T>::type>
inline T CooerceType(sqlite3_value* argv) {
  PERFETTO_CHECK(sqlite3_value_type(argv) == SQLITE_INTEGER);
  return static_cast<T>(sqlite3_value_int64(argv));
}

template <class T>
inline void FilterColumn(const std::deque<T>& data,
                         const QueryConstraints::Constraint& constraint,
                         sqlite3_value* argv,
                         std::vector<bool>* filter) {
  auto it = std::find(filter->begin(), filter->end(), true);
  while (it != filter->end()) {
    auto index = static_cast<size_t>(std::distance(filter->begin(), it));
    switch (constraint.op) {
      case SQLITE_INDEX_CONSTRAINT_EQ:
        *it = data[index] == CooerceType<T>(argv);
        break;
      case SQLITE_INDEX_CONSTRAINT_GE:
        *it = data[index] >= CooerceType<T>(argv);
        break;
      case SQLITE_INDEX_CONSTRAINT_GT:
        *it = data[index] > CooerceType<T>(argv);
        break;
      case SQLITE_INDEX_CONSTRAINT_LE:
        *it = data[index] <= CooerceType<T>(argv);
        break;
      case SQLITE_INDEX_CONSTRAINT_LT:
        *it = data[index] < CooerceType<T>(argv);
        break;
      case SQLITE_INDEX_CONSTRAINT_NE:
        *it = data[index] != CooerceType<T>(argv);
        break;
      default:
        PERFETTO_CHECK(false);
    }
    it = std::find(it + 1, filter->end(), true);
  }
}

}  // namespace sqlite_utils
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_SQLITE_UTILS_H_
