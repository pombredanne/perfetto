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

#include "src/trace_processor/sched_slice_table.h"

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

}  // namespace

SchedSliceTable::SchedSliceTable(const TraceStorage* storage)
    : storage_(storage) {
  memset(&base_, 0, sizeof(base_));
}

int SchedSliceTable::Open(sqlite3_vtab_cursor** ppCursor) {
  *ppCursor =
      reinterpret_cast<sqlite3_vtab_cursor*>(new Cursor(this, storage_));
  return SQLITE_OK;
}

int SchedSliceTable::Close(sqlite3_vtab_cursor* cursor) {
  delete GetCursor(cursor);
  return SQLITE_OK;
}

SchedSliceTable::Cursor* SchedSliceTable::GetCursor(
    sqlite3_vtab_cursor* cursor) {
  return reinterpret_cast<Cursor*>(cursor);
}

int SchedSliceTable::BestIndex(sqlite3_index_info* idx) {
  bool external_ordering_required = false;
  for (int i = 0; i < idx->nOrderBy; i++) {
    if (idx->aOrderBy[i].iColumn != Column::kTimestamp ||
        idx->aOrderBy[i].desc) {
      external_ordering_required = true;
      break;
    }
  }
  idx->orderByConsumed = !external_ordering_required;

  indexed_constraints_.emplace_back();
  idx->idxNum = static_cast<int>(indexed_constraints_.size());

  std::vector<Constraint>* constraints = &indexed_constraints_.back();
  for (int i = 0; i < idx->nConstraint; i++) {
    const auto& cs = idx->aConstraint[i];
    if (cs.usable) {
      constraints->emplace_back(cs);
      idx->aConstraintUsage[i].argvIndex =
          static_cast<int>(constraints->size() - 1);
    }
  }
  return SQLITE_OK;
}

SchedSliceTable::Cursor::Cursor(SchedSliceTable* table,
                                const TraceStorage* storage)
    : table_(table), storage_(storage) {
  memset(&base_, 0, sizeof(base_));
}

int SchedSliceTable::Cursor::Filter(int idxNum,
                                    const char* /* idxStr */,
                                    int argc,
                                    sqlite3_value** argv) {
  const auto& constraints =
      table_->indexed_constraints_[static_cast<size_t>(idxNum)];
  PERFETTO_CHECK(constraints.size() == static_cast<size_t>(argc));
  for (size_t i = 0; i < constraints.size(); i++) {
    const auto& cs = constraints[i];
    bool constraint_implemented = false;
    if (cs.iColumn == Column::kTimestamp) {
      constraint_implemented = timestamp_constraints_.Setup(cs, argv[i]);
    } else if (cs.iColumn == Column::kCpu) {
      constraint_implemented = cpu_constraints_.Setup(cs, argv[i]);
    }

    if (!constraint_implemented) {
      PERFETTO_ELOG("Constraint: col:%d op:%d not implemented", cs.iColumn,
                    cs.op);
      return SQLITE_ERROR;
    }
  }

  // TODO: setup the cursor to use the next item.
  perfetto::base::ignore_result(storage_);

  table_->indexed_constraints_.clear();
  return SQLITE_OK;
}

int SchedSliceTable::Cursor::Next() {
  return SQLITE_OK;
}

int SchedSliceTable::Cursor::Eof() {
  return true;
}

int SchedSliceTable::Cursor::Column(sqlite3_context* /* context */,
                                    int /* N */) {
  return SQLITE_OK;
}

int SchedSliceTable::Cursor::RowId(sqlite_int64* /* pRowid */) {
  return SQLITE_ERROR;
}

template <typename T>
bool SchedSliceTable::Cursor::NumericConstraints<T>::Setup(
    const Constraint& cs,
    sqlite3_value* value) {
  bool is_integral = std::is_integral<T>::value;
  PERFETTO_DCHECK(is_integral ? sqlite3_value_type(value) == SQLITE_INTEGER
                              : sqlite3_value_type(value) == SQLITE_FLOAT);
  bool constraint_implemented = true;
  T const_value = static_cast<T>(is_integral ? sqlite3_value_int64(value)
                                             : sqlite3_value_double(value));
  if (IsOpGe(cs.op) || IsOpGt(cs.op)) {
    min_value = const_value;
    min_equals = IsOpGt(cs.op);
  } else if (IsOpLe(cs.op) || IsOpLt(cs.op)) {
    max_value = const_value;
    max_equals = IsOpLt(cs.op);
  } else if (IsOpEq(cs.op)) {
    max_value = const_value;
    max_equals = true;
    min_value = const_value;
    min_equals = true;
  } else {
    constraint_implemented = false;
  }
  return constraint_implemented;
}

}  // namespace trace_processor
}  // namespace perfetto
