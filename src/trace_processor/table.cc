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

Table::~Table() {}

void Table::RegisterTable(RegisterArgs* args) {
  auto* map = GetModuleMapInstance();
  PERFETTO_DCHECK(map->find(args->table_name_) == map->end());

  auto it = map->emplace(args->table_name_, CreateModule());
  auto* module = &it.first->second;
  sqlite3_create_module_v2(
      args->db_, args->table_name_.c_str(), module, args,
      [](void* inner) { delete static_cast<RegisterArgs*>(inner); });
}

int Table::Open(sqlite3_vtab_cursor** ppCursor) {
  *ppCursor = static_cast<sqlite3_vtab_cursor*>(CreateCursor().release());
  return SQLITE_OK;
}

sqlite3_module Table::CreateModule() {
  sqlite3_module module;
  memset(&module, 0, sizeof(module));
  module.xConnect = [](sqlite3* db, void* raw_args, int, const char* const*,
                       sqlite3_vtab** tab, char**) {
    auto* args = static_cast<RegisterArgs*>(raw_args);
    int res = sqlite3_declare_vtab(db, args->create_stmt_.c_str());
    if (res != SQLITE_OK)
      return res;
    *tab = static_cast<sqlite3_vtab*>(args->factory_(args->inner_).release());
    return SQLITE_OK;
  };
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
    delete ToCursor(c);
    return SQLITE_OK;
  };
  module.xFilter = [](sqlite3_vtab_cursor* c, int i, const char* s, int a,
                      sqlite3_value** v) {
    return ToCursor(c)->Filter(i, s, a, v);
  };
  module.xNext = [](sqlite3_vtab_cursor* c) { return ToCursor(c)->Next(); };
  module.xEof = [](sqlite3_vtab_cursor* c) { return ToCursor(c)->Eof(); };
  module.xColumn = [](sqlite3_vtab_cursor* c, sqlite3_context* a, int b) {
    return ToCursor(c)->Column(a, b);
  };
  module.xRowid = [](sqlite3_vtab_cursor*, sqlite_int64*) {
    return SQLITE_ERROR;
  };
  module.xFindFunction = [](sqlite3_vtab* t, int, const char* name,
                            void (**fn)(sqlite3_context*, int, sqlite3_value**),
                            void** args) {
    return ToTable(t)->FindFunction(name, fn, args);
  };
  return module;
}

int Table::BestIndex(sqlite3_index_info* idx) {
  QueryConstraints query_constraints;

  for (int i = 0; i < idx->nOrderBy; i++) {
    int column = idx->aOrderBy[i].iColumn;
    bool desc = idx->aOrderBy[i].desc;
    query_constraints.AddOrderBy(column, desc);
  }

  for (int i = 0; i < idx->nConstraint; i++) {
    const auto& cs = idx->aConstraint[i];
    if (!cs.usable)
      continue;
    query_constraints.AddConstraint(cs.iColumn, cs.op);

    // argvIndex is 1-based so use the current size of the vector.
    int argv_index = static_cast<int>(query_constraints.constraints().size());
    idx->aConstraintUsage[i].argvIndex = argv_index;
  }

  BestIndexInfo info;
  info.omit.resize(query_constraints.constraints().size());

  int ret = BestIndex(query_constraints, &info);
  if (ret != SQLITE_OK)
    return ret;

  idx->orderByConsumed = info.order_by_consumed;
  idx->estimatedCost = info.estimated_cost;

  size_t j = 0;
  for (int i = 0; i < idx->nConstraint; i++) {
    const auto& cs = idx->aConstraint[i];
    if (cs.usable)
      idx->aConstraintUsage[i].omit = info.omit[j++];
  }

  if (!info.order_by_consumed)
    query_constraints.ClearOrderBy();

  idx->idxStr = query_constraints.ToNewSqlite3String().release();
  idx->needToFreeIdxStr = true;

  return SQLITE_OK;
}

int Table::FindFunction(const char*, FindFunctionFn, void**) {
  return 0;
};

Table::Cursor::~Cursor() {}

int Table::Cursor::Filter(int,
                          const char* idxStr,
                          int argc,
                          sqlite3_value** argv) {
  QueryConstraints qc = QueryConstraints::FromString(idxStr);
  PERFETTO_DCHECK(qc.constraints().size() == static_cast<size_t>(argc));
  return Filter(qc, argv);
}

Table::RegisterArgs::RegisterArgs(std::string create_stmt,
                                  std::string table_name,
                                  TableFactory factory,
                                  const void* inner)
    : create_stmt_(create_stmt),
      table_name_(table_name),
      factory_(factory),
      inner_(inner) {}

}  // namespace trace_processor
}  // namespace perfetto
