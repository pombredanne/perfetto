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
#include "sqlite3.h"

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {
namespace {
const char* FindNextInteger(const char* idxStr) {
  const char* i = idxStr;
  for (; *i != ',' && *i != '\0'; ++i) {
  }

  return *i == '\0' ? nullptr : i + 1;
}
}  // namespace

char* QueryConstraints::ToNewSqlite3String() {
  std::vector<std::string> vec_result;
  vec_result.emplace_back(std::to_string(constraints_.size()));
  for (const auto& cs : constraints_) {
    vec_result.emplace_back(std::to_string(cs.iColumn));
    vec_result.emplace_back(std::to_string(cs.op));
  }
  vec_result.emplace_back(std::to_string(order_by_.size()));
  for (const auto& ob : order_by_) {
    vec_result.emplace_back(std::to_string(ob.column));
    vec_result.emplace_back(std::to_string(ob.desc));
  }
  int total_size = 0;
  for (const auto& part : vec_result) {
    total_size += part.size();
  }
  total_size += vec_result.size();

  char* result = static_cast<char*>(sqlite3_malloc(total_size));
  int offset = 0;
  for (const auto& part : vec_result) {
    strncpy(result + offset, part.c_str(), part.size());
    result[offset + static_cast<int>(part.size())] = ',';
    offset += part.size() + 1;
  }
  PERFETTO_DCHECK(offset == total_size);
  result[total_size - 1] = '\0';

  return result;
}

QueryConstraints QueryConstraints::FromString(const char* idxStr) {
  QueryConstraints qc;

  const char* start = idxStr;

  int c_length = atoi(start);
  for (int i = 0; i < c_length; ++i) {
    start = FindNextInteger(start);
    int col = atoi(start);
    start = FindNextInteger(start);
    unsigned char op = static_cast<unsigned char>(atoi(start));
    qc.AddConstraint(col, op);
  }

  start = FindNextInteger(start);
  int ob_length = atoi(start);
  for (int i = 0; i < ob_length; ++i) {
    start = FindNextInteger(start);
    int col = atoi(start);
    start = FindNextInteger(start);
    bool desc = static_cast<bool>(atoi(start));
    qc.AddOrderBy(col, desc);
  }
  PERFETTO_DCHECK(FindNextInteger(start) == nullptr);
  return qc;
}

}  // namespace trace_processor
}  // namespace perfetto
