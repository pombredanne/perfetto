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

#include "src/trace_processor/table.h"

#include <cstring>

#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

Table::Table() {}

sqlite3_module Table::CreateModule() {
  sqlite3_module module;
  memset(&module, 0, sizeof(module));
  module.xBestIndex = [](sqlite3_vtab* t, sqlite3_index_info* i) {
    return ToTable(t)->BestIndex(i);
  };
  module.xDisconnect = [](sqlite3_vtab* t) {
    delete ToTable(t);
    return SQLITE_OK;
  };
  module.xOpen = [](sqlite3_vtab* t, sqlite3_vtab_cursor** c) {
    return ToTable(t)->Open(c);
  };
  module.xClose = [](sqlite3_vtab_cursor* c) {
    delete ToCusor(c);
    return SQLITE_OK;
  };
  module.xFilter = [](sqlite3_vtab_cursor* c, int i, const char* s, int a,
                      sqlite3_value** v) {
    return ToCusor(c)->Filter(i, s, a, v);
  };
  module.xNext = [](sqlite3_vtab_cursor* c) { return ToCusor(c)->Next(); };
  module.xEof = [](sqlite3_vtab_cursor* c) { return ToCusor(c)->Eof(); };
  module.xColumn = [](sqlite3_vtab_cursor* c, sqlite3_context* a, int b) {
    return ToCusor(c)->Column(a, b);
  };
  return module;
}

}  // namespace trace_processor
}  // namespace perfetto
