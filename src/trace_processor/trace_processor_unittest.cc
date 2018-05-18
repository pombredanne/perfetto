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

#include "src/trace_processor/trace_processor.h"

#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {

const char kSql[] = "SELECT * from trace_processor";

TEST(TraceProcessorTest, Test) {
  sqlite3* db;
  sqlite3_open(":memory", &db);

  sqlite3_module module = TraceProcessor::CreateSqliteModule();
  sqlite3_create_module_v2(db, "trace_processor", &module, nullptr, nullptr);

  sqlite3_stmt* statement;
  sqlite3_prepare_v2(db, kSql, sizeof(kSql), &statement, nullptr);

  ASSERT_EQ(sqlite3_step(statement), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(statement, -1), 0);
  ASSERT_EQ(sqlite3_column_int(statement, 0), 100);
  ASSERT_EQ(sqlite3_step(statement), SQLITE_DONE);

  sqlite3_finalize(statement);

  sqlite3_close(db);
}

}  // namespace trace_processor
}  // namespace perfetto
