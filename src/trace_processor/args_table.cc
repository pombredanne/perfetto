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

#include "src/trace_processor/args_table.h"

namespace perfetto {
namespace trace_processor {

ArgsTable::ArgsTable(sqlite3*, const TraceStorage* storage)
    : StorageTable(storage, CreateColumns(storage), {"id", "key"}) {}

void ArgsTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<ArgsTable>(db, storage, "args");
}

std::vector<std::unique_ptr<StorageSchema::Column>> ArgsTable::CreateColumns(
    const TraceStorage* storage) {
  const auto& args = storage->args();
  std::unique_ptr<StorageSchema::Column> cols[] = {
      std::unique_ptr<IdColumn>(new IdColumn("id", storage, &args.ids())),
      StorageSchema::StringColumnPtr("flat_key", &args.flat_keys(),
                                     &storage->string_pool()),
      StorageSchema::StringColumnPtr("key", &args.keys(),
                                     &storage->string_pool()),
      std::unique_ptr<ValueColumn>(
          new ValueColumn("int_value", VarardicType::kInt, storage)),
      std::unique_ptr<ValueColumn>(
          new ValueColumn("string_value", VarardicType::kString, storage)),
      std::unique_ptr<ValueColumn>(
          new ValueColumn("real_value", VarardicType::kReal, storage))};
  return {
      std::make_move_iterator(std::begin(cols)),
      std::make_move_iterator(std::end(cols)),
  };
}

int ArgsTable::BestIndex(const QueryConstraints& qc, BestIndexInfo* info) {
  // In the case of an id equality filter, we can do a very efficient lookup.
  if (qc.constraints().size() == 1) {
    auto id = static_cast<int>(schema_.ColumnIndexFromName("id"));
    const auto& cs = qc.constraints().back();
    if (cs.iColumn == id && sqlite_utils::IsOpEq(cs.op)) {
      info->estimated_cost = 1;
      return SQLITE_OK;
    }
  }

  // Otherwise, just give the worst case scenario.
  info->estimated_cost = static_cast<uint32_t>(storage_->args().args_count());
  return SQLITE_OK;
}

ArgsTable::IdColumn::IdColumn(std::string col_name,
                              const TraceStorage* storage,
                              const std::deque<RowId>* ids)
    : NumericColumn(col_name, ids, false, false), storage_(storage) {}

void ArgsTable::IdColumn::Filter(int op,
                                 sqlite3_value* value,
                                 FilteredRowIndex* index) const {
  if (!sqlite_utils::IsOpEq(op)) {
    NumericColumn::Filter(op, value, index);
    return;
  }
  auto id = sqlite_utils::ExtractSqliteValue<RowId>(value);
  const auto& args_for_id = storage_->args().args_for_id();
  auto it_pair = args_for_id.equal_range(id);

  auto size = static_cast<size_t>(std::distance(it_pair.first, it_pair.second));
  std::vector<uint32_t> rows(size);
  size_t i = 0;
  for (auto it = it_pair.first; it != it_pair.second; it++) {
    rows[i++] = it->second;
  }
  index->IntersectRows(std::move(rows));
}

ArgsTable::ValueColumn::ValueColumn(std::string col_name,
                                    VarardicType type,
                                    const TraceStorage* storage)
    : Column(col_name, false), type_(type), storage_(storage) {}

void ArgsTable::ValueColumn::ReportResult(sqlite3_context* ctx,
                                          uint32_t row) const {
  const auto& value = storage_->args().arg_values()[row];
  if (value.type != type_) {
    sqlite3_result_null(ctx);
    return;
  }

  switch (type_) {
    case VarardicType::kInt:
      sqlite_utils::ReportSqliteResult(ctx, value.int_value);
      break;
    case VarardicType::kReal:
      sqlite_utils::ReportSqliteResult(ctx, value.real_value);
      break;
    case VarardicType::kString: {
      const auto kSqliteStatic = reinterpret_cast<sqlite3_destructor_type>(0);
      const char* str = storage_->GetString(value.string_value).c_str();
      sqlite3_result_text(ctx, str, -1, kSqliteStatic);
      break;
    }
  }
}

ArgsTable::ValueColumn::Bounds ArgsTable::ValueColumn::BoundFilter(
    int,
    sqlite3_value*) const {
  return Bounds{};
}

void ArgsTable::ValueColumn::Filter(int op,
                                    sqlite3_value* value,
                                    FilteredRowIndex* index) const {
  switch (type_) {
    case VarardicType::kInt: {
      auto binary_op = sqlite_utils::GetPredicateForOp<int64_t>(op);
      int64_t extracted = sqlite_utils::ExtractSqliteValue<int64_t>(value);
      index->FilterRows([this, &binary_op, extracted](uint32_t row) {
        const auto& arg = storage_->args().arg_values()[row];
        return arg.type == type_ && binary_op(arg.int_value, extracted);
      });
      break;
    }
    case VarardicType::kReal: {
      auto binary_op = sqlite_utils::GetPredicateForOp<double>(op);
      double extracted = sqlite_utils::ExtractSqliteValue<double>(value);
      index->FilterRows([this, &binary_op, extracted](uint32_t row) {
        const auto& arg = storage_->args().arg_values()[row];
        return arg.type == type_ && binary_op(arg.real_value, extracted);
      });
      break;
    }
    case VarardicType::kString: {
      auto binary_op = sqlite_utils::GetPredicateForOp<std::string>(op);
      const auto* extracted =
          reinterpret_cast<const char*>(sqlite3_value_text(value));
      index->FilterRows([this, &binary_op, extracted](uint32_t row) {
        const auto& arg = storage_->args().arg_values()[row];
        const auto& str = storage_->GetString(arg.string_value);
        return arg.type == type_ && binary_op(str, extracted);
      });
      break;
    }
  }
}

ArgsTable::ValueColumn::Comparator ArgsTable::ValueColumn::Sort(
    const QueryConstraints::OrderBy& ob) const {
  if (ob.desc) {
    return [this](uint32_t f, uint32_t s) { return -CompareRefsAsc(f, s); };
  }
  return [this](uint32_t f, uint32_t s) { return CompareRefsAsc(f, s); };
}

int ArgsTable::ValueColumn::CompareRefsAsc(uint32_t f, uint32_t s) const {
  const auto& arg_f = storage_->args().arg_values()[f];
  const auto& arg_s = storage_->args().arg_values()[s];

  if (arg_f.type == type_ && arg_s.type == type_) {
    switch (type_) {
      case VarardicType::kInt:
        return sqlite_utils::CompareValuesAsc(arg_f.int_value, arg_s.int_value);
      case VarardicType::kReal:
        return sqlite_utils::CompareValuesAsc(arg_f.real_value,
                                              arg_s.real_value);
      case VarardicType::kString: {
        const auto& f_str = storage_->GetString(arg_f.string_value);
        const auto& s_str = storage_->GetString(arg_s.string_value);
        return sqlite_utils::CompareValuesAsc(f_str, s_str);
      }
    }
  } else if (arg_s.type == type_) {
    return -1;
  } else if (arg_f.type == type_) {
    return 1;
  }
  return 0;
}

}  // namespace trace_processor
}  // namespace perfetto
