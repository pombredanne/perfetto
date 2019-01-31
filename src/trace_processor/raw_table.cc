/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/raw_table.h"

#include <inttypes.h>

#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

RawTable::RawTable(sqlite3* db, const TraceStorage* storage)
    : storage_(storage) {
  auto fn = [](sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    auto* thiz = static_cast<RawTable*>(sqlite3_user_data(ctx));
    thiz->ToSystrace(ctx, argc, argv);
  };
  sqlite3_create_function(db, "systrace", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                          this, fn, nullptr, nullptr);
}

void RawTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<RawTable>(db, storage, "raw");
}

StorageSchema RawTable::CreateStorageSchema() {
  const auto& raw = storage_->raw_events();
  return StorageSchema::Builder()
      .AddColumn<IdColumn>("id", TableId::kRawEvents)
      .AddOrderedNumericColumn("ts", &raw.timestamps())
      .AddStringColumn("name", &raw.name_ids(), &storage_->string_pool())
      .AddNumericColumn("cpu", &raw.cpus())
      .AddNumericColumn("utid", &raw.utids())
      .AddNumericColumn("arg_set_id", &raw.arg_set_ids())
      .Build({"name", "ts"});
}

uint32_t RawTable::RowCount() {
  return static_cast<uint32_t>(storage_->raw_events().raw_event_count());
}

int RawTable::BestIndex(const QueryConstraints& qc, BestIndexInfo* info) {
  info->estimated_cost = RowCount();

  // Only the string columns are handled by SQLite
  info->order_by_consumed = true;
  size_t name_index = schema().ColumnIndexFromName("name");
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    info->omit[i] = qc.constraints()[i].iColumn != static_cast<int>(name_index);
  }

  return SQLITE_OK;
}

int RawTable::FormatSystraceArgs(ArgSetId arg_set_id, char* line, size_t) {
  const auto& args = storage_->args();

  const auto& set_ids = args.set_ids();
  auto lb = std::lower_bound(set_ids.begin(), set_ids.end(), arg_set_id);
  auto ub = std::upper_bound(lb, set_ids.end(), arg_set_id);

  int n = 0;
  for (auto it = lb; it < ub; it++) {
    auto arg_row = static_cast<uint32_t>(std::distance(set_ids.begin(), it));
    if (it != lb)
      line[n++] = ' ';

    const auto& key = storage_->GetString(args.keys()[arg_row]);
    memcpy(&line[n], key.c_str(), key.size());
    n += key.size();

    line[n++] = '=';

    const auto& value = args.arg_values()[arg_row];
    switch (value.type) {
      case TraceStorage::Args::Variadic::kInt:
        n += sprintf(&line[n], "%" PRId64, value.int_value);
        break;
      case TraceStorage::Args::Variadic::kReal:
        n += sprintf(&line[n], "%f", value.real_value);
        break;
      case TraceStorage::Args::Variadic::kString: {
        const auto& str = storage_->GetString(value.string_value);
        memcpy(&line[n], str.c_str(), str.size());
        n += str.size();
      }
    }
  }
  return n;
}

void RawTable::ToSystrace(sqlite3_context* ctx,
                          int argc,
                          sqlite3_value** argv) {
  if (argc != 1 || sqlite3_value_type(argv[0]) != SQLITE_INTEGER) {
    sqlite3_result_error(ctx, "Usage: systrace(id)", -1);
    return;
  }
  RowId id = sqlite3_value_int64(argv[0]);
  auto pair = TraceStorage::ParseRowId(id);
  auto row = pair.second;

  const auto& raw = storage_->raw_events();

  UniqueTid utid = raw.utids()[row];
  const auto& thread = storage_->GetThread(utid);
  uint32_t tgid = 0;
  if (thread.upid.has_value()) {
    tgid = storage_->GetProcess(thread.upid.value()).pid;
  }
  const auto& name = storage_->GetString(thread.name_id);

  char line[2048];
  int n = 0;
  n += ftrace_utils::FormatSystracePrefix(
      raw.timestamps()[row], raw.cpus()[row], thread.tid, tgid,
      base::StringView(name), line, sizeof(line));
  PERFETTO_CHECK(static_cast<size_t>(n) < sizeof(line));

  const auto& str = storage_->GetString(raw.name_ids()[row]);
  memcpy(&line[n], str.c_str(), str.size());
  n += str.size();

  constexpr char kNameSuffix[] = ": ";
  memcpy(&line[n], kNameSuffix, sizeof(kNameSuffix));
  n += sizeof(kNameSuffix);

  FormatSystraceArgs(raw.arg_set_ids()[row], &line[n],
                     sizeof(line) - static_cast<size_t>(n));
  sqlite3_result_text(ctx, line, -1, sqlite_utils::kSqliteTransient);
}

}  // namespace trace_processor
}  // namespace perfetto
