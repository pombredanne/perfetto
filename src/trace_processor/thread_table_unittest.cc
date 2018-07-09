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

#include "src/trace_processor/thread_table.h"
#include "src/trace_processor/process_table.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

class ThreadTableUnittest : public ::testing::Test {
 public:
  ThreadTableUnittest() {
    sqlite3_open(":memory:", &db_);

    static sqlite3_module t_module = ThreadTable::CreateModule();
    sqlite3_create_module(db_, "thread", &t_module,
                          static_cast<void*>(&storage_));
    static sqlite3_module p_module = ProcessTable::CreateModule();
    sqlite3_create_module(db_, "process", &p_module,
                          static_cast<void*>(&storage_));
  }

  virtual ~ThreadTableUnittest() {
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

TEST_F(ThreadTableUnittest, Select) {
  uint32_t cpu = 3;
  uint64_t timestamp = 100;
  uint32_t pid_1 = 1;
  uint32_t prev_state = 32;
  static const char kCommProc1[] = "thread1";
  static const char kCommProc2[] = "thread2";
  uint32_t pid_2 = 4;

  storage_.PushSchedSwitch(cpu, timestamp, pid_1, prev_state, kCommProc1,
                           sizeof(kCommProc1) - 1, pid_2);
  storage_.PushSchedSwitch(cpu, timestamp + 1, pid_2, prev_state, kCommProc2,
                           sizeof(kCommProc2) - 1, pid_1);

  storage_.PushProcess(2, "test", 4);
  storage_.MatchThreadToProcess(1, 2);
  PrepareValidStatement("SELECT utid, upid, name FROM thread");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 1 /* utid */);
  ASSERT_EQ(sqlite3_column_int(stmt_, 1), 1 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 2)),
              kCommProc1, 7),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

TEST_F(ThreadTableUnittest, JoinWithProcess) {
  uint32_t cpu = 3;
  uint64_t timestamp = 100;
  uint32_t pid_1 = 1;
  uint32_t prev_state = 32;
  static const char kCommProc1[] = "thread1";
  static const char kCommProc2[] = "thread2";
  uint32_t pid_2 = 4;

  storage_.PushSchedSwitch(cpu, timestamp, pid_1, prev_state, kCommProc1,
                           sizeof(kCommProc1) - 1, pid_2);
  storage_.PushSchedSwitch(cpu, timestamp + 1, pid_2, prev_state, kCommProc2,
                           sizeof(kCommProc2) - 1, pid_1);

  storage_.PushProcess(2, "test", 4);
  storage_.PushProcess(3, "test1", 5);
  storage_.MatchThreadToProcess(1, 2);
  PrepareValidStatement(
      "SELECT utid, thread.name, process.upid, process.name FROM thread INNER "
      "JOIN process USING (upid)");

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_ROW);
  ASSERT_EQ(sqlite3_column_int(stmt_, 0), 1 /* utid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 1)),
              kCommProc1, 7),
      0);
  ASSERT_EQ(sqlite3_column_int(stmt_, 2), 1 /* upid */);
  ASSERT_EQ(
      strncmp(reinterpret_cast<const char*>(sqlite3_column_text(stmt_, 3)),
              "test", 4),
      0);

  ASSERT_EQ(sqlite3_step(stmt_), SQLITE_DONE);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
