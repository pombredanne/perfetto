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

#include "gtest/gtest.h"
#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {
namespace {
using SqliteString =
    base::ScopedResource<char*, QueryConstraints::FreeSqliteString, nullptr>;

TEST(QueryConstraintsTest, ConvertToAndFromSqlString) {
  QueryConstraints qc;
  qc.AddConstraint(12, 0);
  qc.AddOrderBy(1, false);
  qc.AddOrderBy(21, true);

  SqliteString result = qc.ToNewSqlite3String();
  ASSERT_TRUE(strcmp(result.get(), "C1,12,0,O2,1,0,21,1") == 0);

  QueryConstraints qc_result = QueryConstraints::FromString(result.get());

  for (size_t i = 0; i < qc.constraints().size(); i++) {
    ASSERT_EQ(qc.constraints()[i].iColumn, qc_result.constraints()[i].iColumn);
    ASSERT_EQ(qc.constraints()[i].op, qc_result.constraints()[i].op);
  }

  for (size_t i = 0; i < qc.order_by().size(); i++) {
    ASSERT_EQ(qc.order_by()[i].iColumn, qc_result.order_by()[i].iColumn);
    ASSERT_EQ(qc.order_by()[i].desc, qc_result.order_by()[i].desc);
  }
}

TEST(QueryConstraintsTest, CheckEmptyConstraints) {
  QueryConstraints qc;

  SqliteString string_result = qc.ToNewSqlite3String();
  ASSERT_TRUE(strcmp(string_result.get(), "C0,O0") == 0);

  QueryConstraints qc_result =
      QueryConstraints::FromString(string_result.get());
  ASSERT_EQ(qc_result.constraints().size(), 0);
  ASSERT_EQ(qc_result.order_by().size(), 0);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
