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

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {

namespace {

inline bool IsOpEq(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_EQ;
}

inline bool IsOpGe(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_GE;
}

inline bool IsOpGt(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_GT;
}

inline bool IsOpLe(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_LE;
}

inline bool IsOpLt(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_LT;
}

inline ProcessTable* AsTable(sqlite3_vtab* vtab) {
  return reinterpret_cast<ProcessTable*>(vtab);
}

}  // namespace

ProcessTable::ProcessTable(const TraceStorage* storage) : storage_(storage) {
  static_assert(offsetof(ProcessTable, base_) == 0,
                "SQLite base class must be first member of the table");
  memset(&base_, 0, sizeof(base_));
}

sqlite3_module ProcessTable::CreateModule() {
  sqlite3_module module;
  memset(&module, 0, sizeof(module));
  module.xConnect = [](sqlite3* db, void* raw_args, int, const char* const*,
                       sqlite3_vtab** tab, char**) {
    int res = sqlite3_declare_vtab(db,
                                   "CREATE TABLE processes("
                                   "upid UNSIGNED INT, "
                                   "name TEXT, "
                                   "PRIMARY KEY(upid)"
                                   ") WITHOUT ROWID;");
    if (res != SQLITE_OK)
      return res;
    TraceStorage* storage = static_cast<TraceStorage*>(raw_args);
    *tab = reinterpret_cast<sqlite3_vtab*>(new ProcessTable(storage));
    return SQLITE_OK;
  };
  module.xBestIndex = [](sqlite3_vtab* t, sqlite3_index_info* i) {
    return AsTable(t)->BestIndex(i);
  };
  module.xDisconnect = [](sqlite3_vtab* t) {
    delete AsTable(t);
    return SQLITE_OK;
  };
  module.xOpen = [](sqlite3_vtab* t, sqlite3_vtab_cursor** c) {
    return AsTable(t)->Open(c);
  };
  module.xClose = [](sqlite3_vtab_cursor* c) {
    delete AsCursor(c);
    return SQLITE_OK;
  };
  module.xFilter = [](sqlite3_vtab_cursor* c, int i, const char* s, int a,
                      sqlite3_value** v) {
    return AsCursor(c)->Filter(i, s, a, v);
  };
  module.xNext = [](sqlite3_vtab_cursor* c) { return AsCursor(c)->Next(); };
  module.xEof = [](sqlite3_vtab_cursor* c) { return AsCursor(c)->Eof(); };
  module.xColumn = [](sqlite3_vtab_cursor* c, sqlite3_context* a, int b) {
    return AsCursor(c)->Column(a, b);
  };
  return module;
}

int ProcessTable::Open(sqlite3_vtab_cursor** ppCursor) {
  *ppCursor =
      reinterpret_cast<sqlite3_vtab_cursor*>(new Cursor(this, storage_));
  return SQLITE_OK;
}

// Called at least once but possibly many times before filtering things and is
// the best time to keep track of constriants.
int ProcessTable::BestIndex(sqlite3_index_info* idx) {
  indexes_.emplace_back();
  IndexInfo* index = &indexes_.back();
  for (int i = 0; i < idx->nOrderBy; i++) {
    index->order_by.emplace_back();

    OrderBy* order = &index->order_by.back();
    order->column = static_cast<Column>(idx->aOrderBy[i].iColumn);
    order->desc = idx->aOrderBy[i].desc;
  }
  idx->orderByConsumed = true;

  for (int i = 0; i < idx->nConstraint; i++) {
    const auto& cs = idx->aConstraint[i];
    if (!cs.usable)
      continue;
    index->constraints.emplace_back(cs);

    // argvIndex is 1-based so use the current size of the vector.
    int argv_index = static_cast<int>(index->constraints.size());
    idx->aConstraintUsage[i].argvIndex = argv_index;
  }
  idx->idxNum = static_cast<int>(indexes_.size() - 1);

  return SQLITE_OK;
}

ProcessTable::Cursor::Cursor(ProcessTable* table, const TraceStorage* storage)
    : table_(table), storage_(storage) {
  static_assert(offsetof(Cursor, base_) == 0,
                "SQLite base class must be first member of the cursor");
  memset(&base_, 0, sizeof(base_));
}

int ProcessTable::Cursor::Column(sqlite3_context* context, int N) {
  switch (N) {
    case Column::kUpid: {
      sqlite3_result_int64(context, current_upid);
      break;
    }
    case Column::kName: {
      auto process = storage_->GetProcess(current_upid);
      auto name = storage_->GetString(process.name_id);
      sqlite3_result_text(context, name.c_str(),
                          static_cast<int>(name.length()), nullptr);
      break;
    }
  }
  return SQLITE_OK;
}

int ProcessTable::Cursor::Filter(int idxNum,
                                 const char* /* idxStr */,
                                 int argc,
                                 sqlite3_value** argv) {
  const auto& index = table_->indexes_[static_cast<size_t>(idxNum)];
  PERFETTO_CHECK(index.constraints.size() == static_cast<size_t>(argc));

  min_upid = 1;
  max_upid = static_cast<uint32_t>(storage_->process_count());
  desc = false;
  current_upid = min_upid;

  for (size_t i = 0; i < index.constraints.size(); i++) {
    const auto& cs = index.constraints[i];
    if (cs.iColumn == Column::kUpid) {
      TraceStorage::UniquePid constraint_upid =
          static_cast<TraceStorage::UniquePid>(sqlite3_value_int(argv[i]));
      // Set the range of upids that we are interested in, based on the
      // constraints in the query. Everything between min and max (inclusive)
      // will be returned.
      if (IsOpGe(cs.op) || IsOpGt(cs.op)) {
        constraint_upid = IsOpGt(cs.op) ? constraint_upid + 1 : constraint_upid;
        min_upid = constraint_upid;
      } else if (IsOpLe(cs.op) || IsOpLt(cs.op)) {
        constraint_upid = IsOpLt(cs.op) ? constraint_upid - 1 : constraint_upid;
        max_upid = constraint_upid;
      } else if (IsOpEq(cs.op)) {
        min_upid = constraint_upid;
        max_upid = constraint_upid;
      }
    }
  }
  for (const auto& ob : index.order_by) {
    if (ob.column == Column::kUpid) {
      desc = ob.desc;
      if (desc) {
        current_upid = max_upid;
      } else {
        current_upid = min_upid;
      }
    }
  }

  table_->indexes_.clear();
  return SQLITE_OK;
}  // namespace trace_processor

int ProcessTable::Cursor::Next() {
  if (desc) {
    --current_upid;
  } else {
    ++current_upid;
  }
  return SQLITE_OK;
}

int ProcessTable::Cursor::RowId(sqlite_int64* /* pRowid */) {
  return SQLITE_ERROR;
}

int ProcessTable::Cursor::Eof() {
  if (desc) {
    return current_upid < min_upid;
  } else {
    return current_upid > max_upid;
  }
}

}  // namespace trace_processor
}  // namespace perfetto
