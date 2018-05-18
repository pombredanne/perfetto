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

namespace perfetto {

namespace trace_processor {

namespace {

struct TraceTable {
  sqlite3_vtab vtab;
};

struct TraceCursor {
  sqlite3_vtab_cursor vcursor;
  int row = 0;
};

inline TraceCursor* Cursor(sqlite3_vtab_cursor* cursor) {
  return reinterpret_cast<TraceCursor*>(cursor);
}

int Create(sqlite3* db,
           void*,
           int,
           const char* const*,
           sqlite3_vtab** vtab,
           char**) {
  sqlite3_declare_vtab(db, "CREATE TABLE x(row INTEGER)");
  *vtab = reinterpret_cast<sqlite3_vtab*>(new TraceTable());
  return SQLITE_OK;
}

int BestIndex(sqlite3_vtab*, sqlite3_index_info*) {
  return SQLITE_OK;
}

int Destroy(sqlite3_vtab* vtab) {
  delete reinterpret_cast<TraceTable*>(vtab);
  return SQLITE_OK;
}

int OpenCursor(sqlite3_vtab*, sqlite3_vtab_cursor** cursor) {
  *cursor = reinterpret_cast<sqlite3_vtab_cursor*>(new TraceCursor());
  return SQLITE_OK;
}

int CloseCursor(sqlite3_vtab_cursor* cursor) {
  delete Cursor(cursor);
  return SQLITE_OK;
}

int IsCursorEnd(sqlite3_vtab_cursor* cursor) {
  return Cursor(cursor)->row == 1;
}

int Filter(sqlite3_vtab_cursor*, int, const char*, int, sqlite3_value**) {
  return SQLITE_OK;
}

int AdvanceCursor(sqlite3_vtab_cursor* cursor) {
  if (IsCursorEnd(cursor)) {
    return SQLITE_ERROR;
  }
  Cursor(cursor)->row++;
  return SQLITE_OK;
}

int ValueForCursorAndColumn(sqlite3_vtab_cursor* cursor,
                            sqlite3_context* ctx,
                            int) {
  sqlite3_result_int(ctx, Cursor(cursor)->row + 100);
  return SQLITE_OK;
}

int RowForCursor(sqlite3_vtab_cursor* cursor, sqlite_int64* rowid) {
  *rowid = Cursor(cursor)->row;
  return SQLITE_OK;
}

}  // namespace

sqlite3_module TraceProcessor::CreateSqliteModule() {
  sqlite3_module module;
  module.xCreate = nullptr;
  module.xConnect = &Create;
  module.xBestIndex = &BestIndex;
  module.xDisconnect = &Destroy;
  module.xDestroy = &Destroy;
  module.xOpen = &OpenCursor;
  module.xClose = &CloseCursor;
  module.xEof = &IsCursorEnd;
  module.xFilter = &Filter;
  module.xNext = &AdvanceCursor;
  module.xColumn = &ValueForCursorAndColumn;
  module.xRowid = &RowForCursor;
  return module;
}

}  // namespace trace_processor
}  // namespace perfetto
