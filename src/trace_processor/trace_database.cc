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

#include "src/trace_processor/trace_database.h"

#include <functional>

namespace perfetto {
namespace trace_processor {
namespace {
constexpr uint32_t kTraceChunkSizeB = 16 * 1024 * 1024;  // 16 MB
}

TraceDatabase::TraceDatabase(base::TaskRunner* task_runner)
    : task_runner_(task_runner), weak_factory_(this) {
  sqlite3_open(":memory:", &db_);

  // Setup the sched slice table.
  static sqlite3_module module = SchedSliceTable::CreateModule();
  sqlite3_create_module(db_, "sched_slice", &module,
                        static_cast<void*>(&storage_));
}

TraceDatabase::~TraceDatabase() {
  sqlite3_close(db_);
}

void TraceDatabase::LoadTrace(BlobReader* reader,
                              std::function<void()> callback) {
  // Reset storage and start a new trace parsing task.
  storage_ = {};
  parser_.reset(new TraceParser(reader, &storage_, kTraceChunkSizeB));
  LoadTraceChunk(callback);
}

void TraceDatabase::ExecuteQuery(
    const protos::RawQueryArgs& args,
    std::function<void(protos::RawQueryResult)> callback) {
  const auto& sql = args.sql_query();
  sqlite3_stmt* stmt;
  int err = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()),
                               &stmt, nullptr);
  if (err) {
    callback(protos::RawQueryResult());
    return;
  }

  protos::RawQueryResult proto;
  int column_count = sqlite3_column_count(stmt);
  for (int i = 0; i < column_count; i++) {
    auto* descriptor = proto.add_column_descriptors();
    descriptor->set_name(sqlite3_column_name(stmt, i));

    int type = sqlite3_column_type(stmt, i);
    if (type == SQLITE_INTEGER) {
      descriptor->set_type(protos::RawQueryResult_ColumnDesc_Type_INTEGER);
    } else if (type == SQLITE_FLOAT) {
      descriptor->set_type(protos::RawQueryResult_ColumnDesc_Type_FLOAT);
    } else if (type == SQLITE_TEXT) {
      descriptor->set_type(protos::RawQueryResult_ColumnDesc_Type_STRING);
    } else {
      PERFETTO_FATAL("Unexpected column type found in SQL query");
    }
    // Add the empty column to the proto.
    proto.add_columns();
  }

  int row_count = 0;
  int result = sqlite3_step(stmt);
  while (result == SQLITE_ROW) {
    for (int i = 0; i < column_count; i++) {
      switch (proto.column_descriptors(i).type()) {
        case protos::RawQueryResult_ColumnDesc_Type_INTEGER:
          proto.mutable_columns(i)->add_long_values(
              sqlite3_column_int64(stmt, i));
          break;
        case protos::RawQueryResult_ColumnDesc_Type_FLOAT:
          proto.mutable_columns(i)->add_double_values(
              sqlite3_column_double(stmt, i));
          break;
        case protos::RawQueryResult_ColumnDesc_Type_STRING:
          proto.mutable_columns(i)->add_string_values(
              reinterpret_cast<const char*>(sqlite3_column_text(stmt, i)));
          break;
      }
    }
    row_count++;
    result = sqlite3_step(stmt);
  }
  proto.set_num_records(static_cast<uint64_t>(row_count));

  callback(std::move(proto));
}

void TraceDatabase::LoadTraceChunk(std::function<void()> callback) {
  bool has_more = parser_->ParseNextChunk();
  if (!has_more) {
    callback();
    return;
  }

  auto ptr = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([ptr, callback] {
    if (!ptr)
      return;

    ptr->LoadTraceChunk(callback);
  });
}

}  // namespace trace_processor
}  // namespace perfetto
