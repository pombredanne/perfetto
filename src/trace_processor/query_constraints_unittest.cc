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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "perfetto/base/logging.h"

using testing::ElementsAreArray;
using testing::Matcher;
using testing::Field;
using testing::Pointwise;
using testing::Matches;

namespace perfetto {
namespace trace_processor {
namespace {

Matcher<QueryConstraints::Constraint> MatchesConstraint(
    const QueryConstraints::Constraint& b) {
  return AllOf(Field(&QueryConstraints::Constraint::iColumn, b.iColumn),
               Field(&QueryConstraints::Constraint::op, b.op));
}

MATCHER(MatchConstraint, "") {
  auto& a = ::testing::get<0>(arg);
  auto& b = ::testing::get<1>(arg);
  return Matches(MatchesConstraint(b))(a);
}

Matcher<QueryConstraints::OrderBy> MatchesOrderBy(
    const QueryConstraints::OrderBy& b) {
  return AllOf(Field(&QueryConstraints::OrderBy::iColumn, b.iColumn),
               Field(&QueryConstraints::OrderBy::desc, b.desc));
}

MATCHER(MatchOrderBy, "") {
  auto& a = ::testing::get<0>(arg);
  auto& b = ::testing::get<1>(arg);
  return Matches(MatchesOrderBy(b))(a);
}

TEST(QueryConstraintsTest, ConvertToAndFromSqlString) {
  QueryConstraints qc;
  qc.AddConstraint(12, 0);

  QueryConstraints::SqliteString only_constraint = qc.ToNewSqlite3String();
  ASSERT_TRUE(strcmp(only_constraint.get(), "C1,12,0,O0") == 0);

  QueryConstraints qc_constraint =
      QueryConstraints::FromString(only_constraint.get());
  ASSERT_THAT(qc.constraints(),
              Pointwise(MatchConstraint(), qc_constraint.constraints()));
  ASSERT_THAT(qc.order_by(),
              Pointwise(MatchOrderBy(), qc_constraint.order_by()));

  qc.AddOrderBy(1, false);
  qc.AddOrderBy(21, true);

  QueryConstraints::SqliteString result = qc.ToNewSqlite3String();
  ASSERT_TRUE(strcmp(result.get(), "C1,12,0,O2,1,0,21,1") == 0);

  QueryConstraints qc_result = QueryConstraints::FromString(result.get());

  ASSERT_THAT(qc.constraints(),
              Pointwise(MatchConstraint(), qc_result.constraints()));
  ASSERT_THAT(qc.order_by(), Pointwise(MatchOrderBy(), qc_result.order_by()));
}

TEST(QueryConstraintsTest, CheckEmptyConstraints) {
  QueryConstraints qc;

  QueryConstraints::SqliteString string_result = qc.ToNewSqlite3String();
  ASSERT_TRUE(strcmp(string_result.get(), "C0,O0") == 0);

  QueryConstraints qc_result =
      QueryConstraints::FromString(string_result.get());
  ASSERT_EQ(qc_result.constraints().size(), 0);
  ASSERT_EQ(qc_result.order_by().size(), 0);
}

TEST(QueryConstraintsTest, OnlyOrderBy) {
  QueryConstraints qc;
  qc.AddOrderBy(3, true);

  QueryConstraints::SqliteString string_result = qc.ToNewSqlite3String();
  ASSERT_TRUE(strcmp(string_result.get(), "C0,O1,3,1") == 0);

  QueryConstraints qc_result =
      QueryConstraints::FromString(string_result.get());
  ASSERT_THAT(qc.constraints(),
              Pointwise(MatchConstraint(), qc_result.constraints()));
  ASSERT_THAT(qc.order_by(), Pointwise(MatchOrderBy(), qc_result.order_by()));
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
