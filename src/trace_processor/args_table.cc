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

#include "src/trace_processor/storage_cursor.h"
#include "src/trace_processor/table_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {
const TraceStorage* g_storage;  // TODO: this is a hack.
}

// static
void ArgsTable::ExtractFunction(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
  // sqlite3* db = sqlite3_context_db_handle(ctx);
  PERFETTO_DCHECK(argc == 2);
  uint64_t row_id = static_cast<uint64_t>(sqlite3_value_int64(argv[0]));
  const char* key = reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));

  // TODO don't create if not exists, instroduce MaybeGetInternedString(....).
  StringId key_id = g_storage->LookupInternedString(key);

  ssize_t idx = g_storage->args().Lookup(row_id, key_id);
  if (idx < 0) {
    sqlite3_result_null(ctx);
    return;
  }

  // TODO: return c_str directly.
  const auto& value = g_storage->args().values()[static_cast<size_t>(idx)];
  switch(g_storage->args().values()[static_cast<size_t>(idx)].type) {
    case Variadic::kNull:
      sqlite3_result_null(ctx);
      break;
    case Variadic::kInt:
      sqlite3_result_int64(ctx, value.int_value);
      break;
    case Variadic::kDouble:
      sqlite3_result_double(ctx, value.real_value);
      break;
    case Variadic::kString:
      sqlite3_result_text(ctx, g_storage->GetString(static_cast<StringId>(value.int_value)).c_str(), -1, nullptr);
      break;
  }
}

ArgsTable::ArgsTable(sqlite3*, const TraceStorage* storage)
    : storage_(storage){};

void ArgsTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<ArgsTable>(db, storage, "args");
  g_storage = storage;
}

Table::Schema ArgsTable::CreateSchema(int, const char* const*) {
  const auto& args = storage_->args();
  std::unique_ptr<StorageSchema::Column> cols[] = {
      StorageSchema::NumericColumnPtr("event_id", &args.row_ids(),
                                      false /* hidden */, false /* ordered */),
      StorageSchema::StringColumnPtr("name", &args.names(),
                                     &storage_->string_pool()),
      StorageSchema::VariadicIntColumnPtr("int_value", &args.values(), false /* hidden */),
      StorageSchema::VariadicStrColumnPtr("str_value", &args.values(), false /* hidden */),
  };
  schema_ = StorageSchema({
      std::make_move_iterator(std::begin(cols)),
      std::make_move_iterator(std::end(cols)),
  });
  return schema_.ToTableSchema({"event_id", "name"});
}

std::unique_ptr<Table::Cursor> ArgsTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  uint32_t count = static_cast<uint32_t>(storage_->args().size());
  auto it = table_utils::CreateBestRowIteratorForGenericSchema(schema_, count,
                                                               qc, argv);
  return std::unique_ptr<Table::Cursor>(
      new StorageCursor(std::move(it), schema_.ToColumnReporters()));
}

int ArgsTable::BestIndex(const QueryConstraints&, BestIndexInfo*) {
  // info->estimated_cost =
  //     static_cast<uint32_t>(storage_->counters().counter_count());

  // // Only the string columns are handled by SQLite
  // info->order_by_consumed = true;
  // size_t name_index = schema_.ColumnIndexFromName("name");
  // size_t ref_type_index = schema_.ColumnIndexFromName("ref_type");
  // for (size_t i = 0; i < qc.constraints().size(); i++) {
  //   info->omit[i] =
  //       qc.constraints()[i].iColumn != static_cast<int>(name_index) &&
  //       qc.constraints()[i].iColumn != static_cast<int>(ref_type_index);
  // }

  return SQLITE_OK;
}
}  // namespace trace_processor
}  // namespace perfetto
