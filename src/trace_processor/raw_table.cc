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

#include "src/trace_processor/ftrace_descriptors.h"
#include "src/trace_processor/sqlite_utils.h"

#include "perfetto/trace/ftrace/ftrace_event.pb.h"

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

void RawTable::FormatSystraceArgs(const std::string& event_name,
                                  ArgSetId arg_set_id,
                                  base::StringWriter* writer) {
  const auto& set_ids = storage_->args().set_ids();
  auto lb = std::lower_bound(set_ids.begin(), set_ids.end(), arg_set_id);
  auto ub = std::find(lb, set_ids.end(), arg_set_id + 1);

  auto start_row = static_cast<uint32_t>(std::distance(set_ids.begin(), lb));

  using Variadic = TraceStorage::Args::Variadic;
  using ValueWriter = std::function<void(const Variadic&)>;
  auto write_value = [this, writer](const Variadic& value) {
    switch (value.type) {
      case TraceStorage::Args::Variadic::kInt:
        writer->WriteInt(value.int_value);
        break;
      case TraceStorage::Args::Variadic::kReal:
        writer->WriteDouble(value.real_value);
        break;
      case TraceStorage::Args::Variadic::kString: {
        const auto& str = storage_->GetString(value.string_value);
        writer->WriteString(str.c_str(), str.size());
      }
    }
  };
  auto write_arg = [this, writer, start_row](uint32_t arg_idx,
                                             ValueWriter value_fn) {
    uint32_t arg_row = start_row + arg_idx;
    if (arg_row != 0)
      writer->WriteChar(' ');

    const auto& args = storage_->args();
    const auto& key = storage_->GetString(args.keys()[arg_row]);
    const auto& value = args.arg_values()[arg_row];

    writer->WriteString(key.c_str(), key.length());
    writer->WriteChar('=');
    value_fn(value);
  };

  if (event_name == "sched_switch") {
    using SS = protos::SchedSwitchFtraceEvent;
    write_arg(SS::kPrevCommFieldNumber - 1, write_value);
    write_arg(SS::kPrevPidFieldNumber - 1, write_value);
    write_arg(SS::kPrevPrioFieldNumber - 1, write_value);
    write_arg(SS::kPrevStateFieldNumber - 1, [writer](const Variadic& value) {
      auto state = static_cast<uint16_t>(value.int_value);
      writer->WriteString(ftrace_utils::TaskState(state).ToString().data());
    });

    constexpr char kDelimiter[] = " ==>";
    writer->WriteString(kDelimiter, sizeof(kDelimiter) - 1);
    write_arg(SS::kNextCommFieldNumber - 1, write_value);
    write_arg(SS::kNextPidFieldNumber - 1, write_value);
    write_arg(SS::kNextPrioFieldNumber - 1, write_value);
  }

  uint32_t arg = 0;
  for (auto it = lb; it != ub; it++) {
    write_arg(arg++, write_value);
  }
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
  base::StringWriter writer(line, sizeof(line));

  ftrace_utils::FormatSystracePrefix(raw.timestamps()[row], raw.cpus()[row],
                                     thread.tid, tgid, base::StringView(name),
                                     &writer);

  const auto& event_name = storage_->GetString(raw.name_ids()[row]);
  writer.WriteChar(' ');
  writer.WriteString(event_name.c_str(), event_name.size());

  constexpr char kNameSuffix[] = ": ";
  writer.WriteString(kNameSuffix, sizeof(kNameSuffix) - 1);

  FormatSystraceArgs(event_name, raw.arg_set_ids()[row], &writer);
  sqlite3_result_text(ctx, writer.GetCString(), -1,
                      sqlite_utils::kSqliteTransient);
}

}  // namespace trace_processor
}  // namespace perfetto
