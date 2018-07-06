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

#include "src/trace_processor/query_constraints.h"

#include <string>

#include "include/perfetto/base/string_splitter.h"
#include "sqlite3.h"

namespace perfetto {
namespace trace_processor {
using SqliteString =
    base::ScopedResource<char*, QueryConstraints::FreeSqliteString, nullptr>;

SqliteString QueryConstraints::ToNewSqlite3String() {
  std::string str_result;
  str_result.append("C" + std::to_string(constraints_.size()) + ",");
  for (const auto& cs : constraints_) {
    str_result.append(std::to_string(cs.iColumn) + ",");
    str_result.append(std::to_string(cs.op) + ",");
  }
  str_result.append("O" + std::to_string(order_by_.size()) + ",");
  for (const auto& ob : order_by_) {
    str_result.append(std::to_string(ob.iColumn) + ",");
    str_result.append(std::to_string(ob.desc) + ",");
  }

  int total_size = static_cast<int>(str_result.size());
  char* result = static_cast<char*>(sqlite3_malloc(total_size));
  strncpy(result, str_result.c_str(), str_result.size());
  result[total_size - 1] = '\0';

  return SqliteString(result);
}

QueryConstraints QueryConstraints::FromString(const char* idxStr) {
  QueryConstraints qc;

  const char* current = idxStr;
  base::StringSplitter splitter(std::move(current), ',');

  // The '+ 1' skips the letter 'C' in the first token.
  int num_constraints = splitter.Next() ? atoi(splitter.cur_token() + 1) : 0;
  for (int i = 0; i < num_constraints; ++i) {
    splitter.Next();
    int col = atoi(splitter.cur_token());
    splitter.Next();
    unsigned char op = static_cast<unsigned char>(atoi(splitter.cur_token()));
    qc.AddConstraint(col, op);
  }

  // The '+ 1' skips the letter 'O' in the current token.
  int num_order_by = splitter.Next() ? atoi(splitter.cur_token() + 1) : 0;
  for (int i = 0; i < num_order_by; ++i) {
    splitter.Next();
    int col = atoi(splitter.cur_token());
    splitter.Next();
    unsigned char desc = static_cast<unsigned char>(atoi(splitter.cur_token()));
    qc.AddOrderBy(col, desc);
  }

  PERFETTO_DCHECK(splitter.Next() == false);
  return qc;
}

}  // namespace trace_processor
}  // namespace perfetto
