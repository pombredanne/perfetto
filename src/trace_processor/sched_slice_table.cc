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

#include <string.h>
#include <algorithm>
#include <bitset>
#include <numeric>

#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

using namespace sqlite_utils;

constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();

template <size_t N = base::kMaxCpus>
bool PopulateFilterBitmap(int op,
                          sqlite3_value* value,
                          std::bitset<N>* filter) {
  bool constraint_implemented = true;
  int64_t int_value = sqlite3_value_int64(value);
  if (IsOpGe(op) || IsOpGt(op)) {
    // If the operator is gt, then add one to the upper bound.
    int_value = IsOpGt(op) ? int_value + 1 : int_value;

    // Set to false all values less than |int_value|.
    size_t ub = static_cast<size_t>(std::max<int64_t>(0, int_value));
    ub = std::min(ub, filter->size());
    for (size_t i = 0; i < ub; i++) {
      filter->set(i, false);
    }
  } else if (IsOpLe(op) || IsOpLt(op)) {
    // If the operator is lt, then minus one to the lower bound.
    int_value = IsOpLt(op) ? int_value - 1 : int_value;

    // Set to false all values greater than |int_value|.
    size_t lb = static_cast<size_t>(std::max<int64_t>(0, int_value));
    lb = std::min(lb, filter->size());
    for (size_t i = lb; i < filter->size(); i++) {
      filter->set(i, false);
    }
  } else if (IsOpEq(op)) {
    if (int_value >= 0 && static_cast<size_t>(int_value) < filter->size()) {
      // If the value is in bounds, set all bits to false and restore the value
      // of the bit at the specified index.
      bool existing = filter->test(static_cast<size_t>(int_value));
      filter->reset();
      filter->set(static_cast<size_t>(int_value), existing);
    } else {
      // If the index is out of bounds, nothing should match.
      filter->reset();
    }
  } else {
    constraint_implemented = false;
  }
  return constraint_implemented;
}

template <class T>
inline int Compare(T first, T second, bool desc) {
  if (first < second) {
    return desc ? 1 : -1;
  } else if (first > second) {
    return desc ? -1 : 1;
  }
  return 0;
}

}  // namespace

SchedSliceTable::SchedSliceTable(const TraceStorage* storage)
    : storage_(storage) {}

void SchedSliceTable::RegisterTable(sqlite3* db, const TraceStorage* storage) {
  Table::Register<SchedSliceTable>(db, storage,
                                   "CREATE TABLE sched("
                                   "ts UNSIGNED BIG INT, "
                                   "cpu UNSIGNED INT, "
                                   "dur UNSIGNED BIG INT, "
                                   "quantized_group UNSIGNED BIG INT, "
                                   "utid UNSIGNED INT, "
                                   "cycles UNSIGNED BIG INT, "
                                   "PRIMARY KEY(cpu, ts)"
                                   ") WITHOUT ROWID;");
}

std::unique_ptr<Table::Cursor> SchedSliceTable::CreateCursor() {
  return std::unique_ptr<Table::Cursor>(new Cursor(storage_));
}

int SchedSliceTable::BestIndex(const QueryConstraints& qc,
                               BestIndexInfo* info) {
  bool is_time_constrained = false;
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    const auto& cs = qc.constraints()[i];
    if (cs.iColumn == Column::kTimestamp)
      is_time_constrained = true;
  }

  info->estimated_cost = is_time_constrained ? 10 : 10000;
  info->order_by_consumed = true;

  return SQLITE_OK;
}

int SchedSliceTable::FindFunction(const char* name,
                                  FindFunctionFn fn,
                                  void** args) {
  // Add an identity match function to prevent throwing an exception when
  // matching on the quantum column.
  if (strcmp(name, "match") == 0) {
    *fn = [](sqlite3_context* ctx, int n, sqlite3_value** v) {
      PERFETTO_DCHECK(n == 2 && sqlite3_value_type(v[0]) == SQLITE_INTEGER);
      sqlite3_result_int64(ctx, sqlite3_value_int64(v[0]));
    };
    *args = nullptr;
    return 1;
  }
  return 0;
}

SchedSliceTable::Cursor::Cursor(const TraceStorage* storage)
    : storage_(storage) {}

int SchedSliceTable::Cursor::Filter(const QueryConstraints& qc,
                                    sqlite3_value** argv) {
  filter_state_.reset(new FilterState(storage_, qc, argv));
  return SQLITE_OK;
}

int SchedSliceTable::Cursor::Next() {
  auto* state = filter_state_->StateForCpu(filter_state_->next_cpu());
  state->FindNextSlice();
  filter_state_->FindCpuWithNextSlice();
  return SQLITE_OK;
}

int SchedSliceTable::Cursor::Eof() {
  return !filter_state_->IsNextCpuValid();
}

int SchedSliceTable::Cursor::Column(sqlite3_context* context, int N) {
  if (!filter_state_->IsNextCpuValid())
    return SQLITE_ERROR;

  uint32_t cpu = filter_state_->next_cpu();
  const auto* state = filter_state_->StateForCpu(cpu);
  size_t row = state->next_row_id();
  const auto& slices = storage_->SlicesForCpu(cpu);

  switch (N) {
    case Column::kTimestamp: {
      uint64_t ts = slices.start_ns()[row];
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(ts));
      break;
    }
    case Column::kCpu: {
      sqlite3_result_int(context, static_cast<int>(cpu));
      break;
    }
    case Column::kDuration: {
      uint64_t duration = slices.durations()[row];
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(duration));
      break;
    }
    case Column::kUtid: {
      sqlite3_result_int64(context, slices.utids()[row]);
      break;
    }
    case Column::kCycles: {
      sqlite3_result_int64(context,
                           static_cast<sqlite3_int64>(slices.cycles()[row]));
      break;
    }
  }
  return SQLITE_OK;
}

SchedSliceTable::FilterState::FilterState(
    const TraceStorage* storage,
    const QueryConstraints& query_constraints,
    sqlite3_value** argv)
    : order_by_(query_constraints.order_by()), storage_(storage) {
  std::bitset<base::kMaxCpus> cpu_filter;
  cpu_filter.set();

  uint64_t min_ts = 0;
  uint64_t max_ts = kUint64Max;

  for (size_t i = 0; i < query_constraints.constraints().size(); i++) {
    const auto& cs = query_constraints.constraints()[i];
    switch (cs.iColumn) {
      case Column::kCpu:
        PopulateFilterBitmap(cs.op, argv[i], &cpu_filter);
        break;
      case Column::kTimestamp: {
        auto ts = static_cast<uint64_t>(sqlite3_value_int64(argv[i]));
        if (IsOpGe(cs.op) || IsOpGt(cs.op)) {
          min_ts = IsOpGe(cs.op) ? ts : ts + 1;
        } else if (IsOpLe(cs.op) || IsOpLt(cs.op)) {
          max_ts = IsOpLe(cs.op) ? ts : ts - 1;
        }
        break;
      }
    }
  }

  // Setup CPU filtering because the trace storage is indexed by CPU.
  for (uint32_t cpu = 0; cpu < base::kMaxCpus; cpu++) {
    if (!cpu_filter.test(cpu))
      continue;
    StateForCpu(cpu)->Initialize(
        cpu, storage_, CreateSortedIndexVectorForCpu(cpu, min_ts, max_ts));
  }

  // Set the cpu index to be the first item to look at.
  FindCpuWithNextSlice();
}

void SchedSliceTable::FilterState::FindCpuWithNextSlice() {
  next_cpu_ = base::kMaxCpus;

  for (uint32_t cpu = 0; cpu < base::kMaxCpus; cpu++) {
    const auto& cpu_state = per_cpu_state_[cpu];
    if (!cpu_state.IsNextRowIdIndexValid())
      continue;

    // The first CPU with a valid slice can be set to the next CPU.
    if (next_cpu_ == base::kMaxCpus) {
      next_cpu_ = cpu;
      continue;
    }

    // If the current CPU is ordered before the current "next" CPU, then update
    // the cpu value.
    int cmp = CompareCpuToNextCpu(cpu);
    if (cmp < 0)
      next_cpu_ = cpu;
  }
}

int SchedSliceTable::FilterState::CompareCpuToNextCpu(uint32_t cpu) {
  size_t next_row = per_cpu_state_[next_cpu_].next_row_id();
  size_t row = per_cpu_state_[cpu].next_row_id();
  return CompareSlices(cpu, row, next_cpu_, next_row);
}

std::vector<uint32_t>
SchedSliceTable::FilterState::CreateSortedIndexVectorForCpu(uint32_t cpu,
                                                            uint64_t min_ts,
                                                            uint64_t max_ts) {
  const auto& slices = storage_->SlicesForCpu(cpu);
  const auto& start_ns = slices.start_ns();
  PERFETTO_CHECK(slices.slice_count() <= std::numeric_limits<uint32_t>::max());

  auto min_it = std::lower_bound(start_ns.begin(), start_ns.end(), min_ts);
  auto max_it = std::upper_bound(min_it, start_ns.end(), max_ts);
  ptrdiff_t dist = std::distance(min_it, max_it);
  PERFETTO_CHECK(dist >= 0 && static_cast<size_t>(dist) <= start_ns.size());

  std::vector<uint32_t> indices(static_cast<size_t>(dist));

  // Fill |indices| with the consecutive row numbers affected by the filtering.
  std::iota(indices.begin(), indices.end(),
            std::distance(start_ns.begin(), min_it));

  // In other cases, sort by the given criteria.
  std::sort(indices.begin(), indices.end(),
            [this, cpu](uint32_t f, uint32_t s) {
              return CompareSlices(cpu, f, cpu, s) < 0;
            });
  return indices;
}

int SchedSliceTable::FilterState::CompareSlices(uint32_t f_cpu,
                                                size_t f_idx,
                                                uint32_t s_cpu,
                                                size_t s_idx) {
  for (const auto& ob : order_by_) {
    int c = CompareSlicesOnColumn(f_cpu, f_idx, s_cpu, s_idx, ob);
    if (c != 0)
      return c;
  }
  return 0;
}

int SchedSliceTable::FilterState::CompareSlicesOnColumn(
    uint32_t f_cpu,
    size_t f_idx,
    uint32_t s_cpu,
    size_t s_idx,
    const QueryConstraints::OrderBy& ob) {
  const auto& f_sl = storage_->SlicesForCpu(f_cpu);
  const auto& s_sl = storage_->SlicesForCpu(s_cpu);
  switch (ob.iColumn) {
    case SchedSliceTable::Column::kTimestamp:
      return Compare(f_sl.start_ns()[f_idx], s_sl.start_ns()[s_idx], ob.desc);
    case SchedSliceTable::Column::kDuration:
      return Compare(f_sl.durations()[f_idx], s_sl.durations()[s_idx], ob.desc);
    case SchedSliceTable::Column::kCpu:
      return Compare(f_cpu, s_cpu, ob.desc);
    case SchedSliceTable::Column::kUtid:
      return Compare(f_sl.utids()[f_idx], s_sl.utids()[s_idx], ob.desc);
    case SchedSliceTable::Column::kCycles:
      return Compare(f_sl.cycles()[f_idx], s_sl.cycles()[s_idx], ob.desc);
  }
  PERFETTO_FATAL("Unexpected column %d", ob.iColumn);
}

void SchedSliceTable::PerCpuState::Initialize(
    uint32_t cpu,
    const TraceStorage* storage,
    std::vector<uint32_t> sorted_row_ids) {
  cpu_ = cpu;
  storage_ = storage;
  sorted_row_ids_ = std::move(sorted_row_ids);
}

void SchedSliceTable::PerCpuState::FindNextSlice() {
  next_row_id_index_++;
}

}  // namespace trace_processor
}  // namespace perfetto
