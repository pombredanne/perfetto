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

#ifndef SRC_TRACE_PROCESSOR_QUERY_CONSTRAINTS_H_
#define SRC_TRACE_PROCESSOR_QUERY_CONSTRAINTS_H_

#include <vector>
#include "sqlite3.h"

namespace perfetto {
namespace trace_processor {
// This class stores the constraints (including the order by information) for
// a query on a sqlite3 virtual table.
// The constraints must be converted to and from a const char* to be used
// by sqlite.
class QueryConstraints {
 public:
  using Constraint = sqlite3_index_info::sqlite3_index_constraint;

  struct OrderBy {
    int column = 0;
    bool desc = false;
  };

  void AddConstraint(int column, unsigned char op) {
    Constraint c;
    c.iColumn = column;
    c.op = op;
    constraints_.emplace_back(c);
  }

  void AddOrderBy(int column, bool desc) {
    OrderBy ob;
    ob.column = column;
    ob.desc = desc;
    order_by_.emplace_back(ob);
  }

  void ClearOrderBy() { order_by_.clear(); }

  // Converts the constraints and order by information to a string for
  // use by sqlite.
  char* ToNewSqlite3String();
  static QueryConstraints FromString(const char* encoded_string);

  const std::vector<OrderBy>& order_by() const { return order_by_; }

  const std::vector<Constraint>& constraints() const { return constraints_; }

 private:
  std::vector<OrderBy> order_by_;
  std::vector<Constraint> constraints_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_QUERY_CONSTRAINTS_H_
