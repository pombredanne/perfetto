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

TEST(QueryConstraintsTest, ConvertToAndFromSqlString) {
  QueryConstraints qc;
  qc.AddConstraint(12, 0);
  qc.AddOrderBy(1, false);
  qc.AddOrderBy(21, true);

  const char* result = qc.ToNewSqlite3String();
  ASSERT_TRUE(strcmp(result, "1,12,0,2,1,0,21,1") == 0);

  QueryConstraints back = QueryConstraints::FromString(result);

  for (size_t i = 0; i < qc.constraints().size(); i++) {
    ASSERT_EQ(qc.constraints()[i].iColumn, back.constraints()[i].iColumn);
    ASSERT_EQ(qc.constraints()[i].op, back.constraints()[i].op);
  }

  for (size_t i = 0; i < qc.order_by().size(); i++) {
    ASSERT_EQ(qc.order_by()[i].column, back.order_by()[i].column);
    ASSERT_EQ(qc.order_by()[i].desc, back.order_by()[i].desc);
  }
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
