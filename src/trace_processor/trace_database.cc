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

namespace perfetto {
namespace trace_processor {

TraceDatabase::TraceDatabase(TraceStorage* storage) : storage_(storage) {
  sqlite3_open(":memory:", &db_);

  // Setup the sched slice table.
  sqlite3_module module;
  memset(&module, 0, sizeof(module));
  module.xConnect = [](sqlite3* db, void*, int, const char* const*,
                       sqlite3_vtab** ppVtab, char**) {
    int res = sqlite3_declare_vtab(
        db,
        "CREATE TABLE x(ts, cpu, dur, PRIMARY KEY(cpu, ts)) WITHOUT ROWID;");
    if (res != SQLITE_OK)
      return res;
    TraceDatabase* database = GetDatabase(db);
    *ppVtab = reinterpret_cast<sqlite3_vtab*>(
        new SchedSliceTable(database->storage_));
    return SQLITE_OK;
  };
  module.xBestIndex = [](sqlite3_vtab* t, sqlite3_index_info* i) {
    return GetSchedSliceTable(t)->BestIndex(i);
  };
  module.xDisconnect = [](sqlite3_vtab* t) {
    delete GetSchedSliceTable(t);
    return SQLITE_OK;
  };
  module.xOpen = [](sqlite3_vtab* t, sqlite3_vtab_cursor** c) {
    return GetSchedSliceTable(t)->Open(c);
  };
  module.xClose = [](sqlite3_vtab_cursor* c) {
    delete SchedSliceTable::GetCursor(c);
    return SQLITE_OK;
  };
  module.xFilter = [](sqlite3_vtab_cursor* c, int i, const char* s, int a,
                      sqlite3_value** v) {
    return SchedSliceTable::GetCursor(c)->Filter(i, s, a, v);
  };
  module.xNext = [](sqlite3_vtab_cursor* c) {
    return SchedSliceTable::GetCursor(c)->Next();
  };
  module.xEof = [](sqlite3_vtab_cursor* c) {
    return SchedSliceTable::GetCursor(c)->Eof();
  };
  module.xColumn = [](sqlite3_vtab_cursor* c, sqlite3_context* a, int b) {
    return SchedSliceTable::GetCursor(c)->Column(a, b);
  };
  sqlite3_create_module(db_, "sched_slice", &module, nullptr);
}

TraceDatabase::~TraceDatabase() {
  sqlite3_close(db_);
}

TraceDatabase* TraceDatabase::GetDatabase(sqlite3* db) {
  return reinterpret_cast<TraceDatabase*>(db);
}

SchedSliceTable* TraceDatabase::GetSchedSliceTable(sqlite3_vtab* vtab) {
  return reinterpret_cast<SchedSliceTable*>(vtab);
}

}  // namespace trace_processor
}  // namespace perfetto
