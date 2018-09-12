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

#include "src/trace_processor/counters_table.h"

#include "perfetto/base/logging.h"
#include "src/trace_processor/query_constraints.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

using namespace sqlite_utils;

}  // namespace

CountersTable::CountersTable(const TraceStorage* storage) : storage_(storage) {}

void CountersTable::RegisterTable(sqlite3* db,
                                  const TraceStorage* storage,
                                  std::string name) {
  if (name == "cpufreq") {
    Table::Register<CountersTable>(db, storage,
                                   "CREATE TABLE cpufreq("
                                   "ts UNSIGNED BIG INT, "
                                   "name text, "
                                   "freq UNSIGNED BIG INT, "
                                   "dur UNSIGNED BIG INT, "
                                   "cpu UNSIGNED INT, "
                                   "PRIMARY KEY(name, ts, cpu)"
                                   ") WITHOUT ROWID;");
  } else {
    Table::Register<CountersTable>(db, storage,
                                   "CREATE TABLE counters("
                                   "ts UNSIGNED BIG INT, "
                                   "name text, "
                                   "value UNSIGNED BIG INT, "
                                   "dur UNSIGNED BIG INT, "
                                   "upid UNSIGNED INT, "
                                   "PRIMARY KEY(name, ts, upid)"
                                   ") WITHOUT ROWID;");
  }
}

std::unique_ptr<Table::Cursor> CountersTable::CreateCursor() {
  return std::unique_ptr<Table::Cursor>(new Cursor(storage_, GetName()));
}

int CountersTable::BestIndex(const QueryConstraints& qc, BestIndexInfo* info) {
  info->estimated_cost = 10;

  // If the query has a constraint on the |kRef| field, return a reduced cost
  // because we can do that filter efficiently.
  const auto& constraints = qc.constraints();
  if (constraints.size() == 1 && constraints.front().iColumn == Column::kRef) {
    info->estimated_cost = 1;
  }

  return SQLITE_OK;
}

CountersTable::Cursor::Cursor(const TraceStorage* storage,
                              const std::string& name)
    : storage_(storage) {
  if (name == "cpufreq") {
    type_ = CounterType::CPU_ID;
  } else {
    type_ = CounterType::UPID;
  }
}

int CountersTable::Cursor::Column(sqlite3_context* context, int N) {
  switch (N) {
    case Column::kTimestamp: {
      const auto& val = storage_->GetCounterValues(ref_filter_.current, type_);
      sqlite3_result_int64(context,
                           static_cast<int64_t>(val.timestamps[index_]));
      break;
    }
    case Column::kValue: {
      const auto& val = storage_->GetCounterValues(ref_filter_.current, type_);
      sqlite3_result_int64(context, static_cast<int64_t>(val.values[index_]));
      break;
    }
    case Column::kName: {
      const auto& val = storage_->GetCounterValues(ref_filter_.current, type_);
      sqlite3_result_text(context,
                          storage_->GetString(val.counter_name_id).c_str(), -1,
                          nullptr);
      break;
    }
    case Column::kRef: {
      sqlite3_result_int64(context, ref_filter_.current);
      break;
    }
    case Column::kDuration: {
      const auto& val = storage_->GetCounterValues(ref_filter_.current, type_);
      uint64_t duration = 0;
      if (index_ + 1 < val.size()) {
        duration = val.timestamps[index_ + 1] - val.timestamps[index_];
      }
      sqlite3_result_int64(context, static_cast<int64_t>(duration));
      break;
    }
    default:
      PERFETTO_FATAL("Unknown column %d", N);
      break;
  }
  return SQLITE_OK;
}

int CountersTable::Cursor::Filter(const QueryConstraints& qc,
                                  sqlite3_value** argv) {
  if (type_ == CounterType::UPID) {
    ref_filter_.min = 1;
    ref_filter_.max = static_cast<uint32_t>(storage_->process_count());
  } else {
    ref_filter_.min = 0;
    ref_filter_.max = base::kMaxCpus - 1;
  }

  for (size_t j = 0; j < qc.constraints().size(); j++) {
    const auto& cs = qc.constraints()[j];
    if (cs.iColumn == Column::kRef) {
      auto constraint = static_cast<uint32_t>(sqlite3_value_int(argv[j]));
      if (IsOpEq(cs.op)) {
        ref_filter_.min = constraint;
        ref_filter_.max = constraint;
      } else if (IsOpGe(cs.op) || IsOpGt(cs.op)) {
        ref_filter_.min = IsOpGt(cs.op) ? constraint + 1 : constraint;
      } else if (IsOpLe(cs.op) || IsOpLt(cs.op)) {
        ref_filter_.max = IsOpLt(cs.op) ? constraint - 1 : constraint;
      }
    }
  }
  ref_filter_.current = ref_filter_.min;
  while (ref_filter_.current != ref_filter_.max &&
         storage_->GetCounterValues(ref_filter_.current, type_).size() == 0) {
    ++ref_filter_.current;
  }

  return SQLITE_OK;
}

int CountersTable::Cursor::Next() {
  if (index_ < storage_->GetCounterValues(ref_filter_.current, type_).size()) {
    ++index_;
  } else if (ref_filter_.current < ref_filter_.max) {
    ++ref_filter_.current;
    index_ = 0;
    while (ref_filter_.current != ref_filter_.max &&
           storage_->GetCounterValues(ref_filter_.current, type_).size() == 0) {
      ++ref_filter_.current;
    }
  }
  return SQLITE_OK;
}

int CountersTable::Cursor::Eof() {
  return ref_filter_.max == ref_filter_.current &&
         (storage_->GetCounterValues(ref_filter_.current, type_).size() == 0 ||
          index_ ==
              storage_->GetCounterValues(ref_filter_.current, type_).size());
}

}  // namespace trace_processor
}  // namespace perfetto
