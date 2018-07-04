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

#include "src/trace_processor/process_table.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

class ProcessTableUnittest : public ::testing::Test {
 public:
  ProcessTableUnittest() {
    sqlite3_open(":memory:", &db_);

    static sqlite3_module module = ProcessTable::CreateModule();
    sqlite3_create_module(db_, "process", &module,
                          static_cast<void*>(&storage_));
  }

  virtual ~ProcessTableUnittest() {
    sqlite3_finalize(stmt_);
    sqlite3_close(db_);
  }

  void PrepareValidStatement(const std::string& sql) {
    int size = static_cast<int>(sql.size());
    ASSERT_EQ(sqlite3_prepare_v2(db_, sql.c_str(), size, &stmt_, nullptr), 0);
  }

 protected:
  TraceStorage storage_;
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

TEST_F(ProcessTableUnittest, SelectUpidAndName) {
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  storage_.PushProcess(1, kCommProc1, 8);
  storage_.PushProcess(2, kCommProc2, 8);

  PrepareValidStatement("SELECT upid, name FROM process");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 1 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc1, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 2 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc2, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

TEST_F(ProcessTableUnittest, SelectUpidAndNameWithFilter) {
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  storage_.PushProcess(1, kCommProc1, 8);
  storage_.PushProcess(2, kCommProc2, 8);

  PrepareValidStatement("SELECT upid, name FROM process where upid = 2");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 2 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc2, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

TEST_F(ProcessTableUnittest, SelectUpidAndNameWithOrder) {
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  storage_.PushProcess(1, kCommProc1, 8);
  storage_.PushProcess(2, kCommProc2, 8);

  PrepareValidStatement("SELECT upid, name FROM process ORDER BY upid desc");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 2 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc2, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 1 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc1, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

TEST_F(ProcessTableUnittest, SelectUpidAndNameFilterGt) {
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  storage_.PushProcess(1, kCommProc1, 8);
  storage_.PushProcess(2, kCommProc2, 8);

  PrepareValidStatement("SELECT upid, name FROM process where upid > 1");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 2 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc2, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

TEST_F(ProcessTableUnittest, SelectUpidAndNameFilterName) {
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  storage_.PushProcess(1, kCommProc1, 8);
  storage_.PushProcess(2, kCommProc2, 8);

  PrepareValidStatement(
      "SELECT upid, name FROM process where name = \"process2\"");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 2 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc2, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

TEST_F(ProcessTableUnittest, SelectUpidAndNameFilterDifferentOr) {
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  storage_.PushProcess(1, kCommProc1, 8);
  storage_.PushProcess(2, kCommProc2, 8);

  PrepareValidStatement(
      "SELECT upid, name FROM process where upid = 2 or name = \"process2\"");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 2 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc2, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

TEST_F(ProcessTableUnittest, SelectUpidAndNameFilterSameOr) {
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  storage_.PushProcess(1, kCommProc1, 8);
  storage_.PushProcess(2, kCommProc2, 8);

  PrepareValidStatement(
      "SELECT upid, name FROM process where upid = 1 or upid = 2");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 1 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc1, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 2 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc2, 8),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
