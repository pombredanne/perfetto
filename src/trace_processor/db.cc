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

#include "perfetto/trace_processor/db.h"

#include <stddef.h>
#include <string.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/trace_processor/blob_reader.h"

#include "perfetto/trace/trace_packet.pb.h"

namespace perfetto {
namespace trace_processor {

using namespace ::protozero;
using namespace ::perfetto;

namespace {
DB* g_db = nullptr;
constexpr size_t kChunkSize = 16 * 1024 * 1024;
}  // namespace

DB::DB(base::TaskRunner* task_runner) : task_runner_(task_runner) {
  sqlite3_initialize();
  sqlite3_auto_extension(
      reinterpret_cast<void (*)(void)>(&VirtualTable::InitModule));
  sqlite3_open(":memory:", &db_);
  PERFETTO_CHECK(db_);
  PERFETTO_CHECK(!g_db);
  g_db = this;
}

void DB::Query(const char* query) {
  char* err = nullptr;
  bool header = false;
  auto callback = [](void* arg, int cols, char** col_value, char** col_name) {
    bool& did_write_header = *reinterpret_cast<bool*>(arg);
    if (!did_write_header) {
      for (int i = 0; i < cols; i++)
        printf("%18s", col_name[i]);
      printf("\n");
      did_write_header = true;
    }
    for (int i = 0; i < cols; i++)
      printf("%18s", col_value[i]);
    printf("\n");
    return 0;
  };
  PERFETTO_ILOG("Executing query: \"%s\"", query);
  sqlite3_exec(db_, query, callback, &header, &err);
  if (err)
    PERFETTO_ELOG("%s", err);
}

// +---------------------------------------------------------------------------+
// | These functions decode the trace and store it in in-mem columnar storage  |
// +---------------------------------------------------------------------------+
void DB::LoadTrace(BlobReader* reader) {
  reader_ = reader;
  off_ = 0;
  PERFETTO_ILOG("Starting indexing, please be patient...");
  load_start_time_ = perfetto::base::GetWallTimeMs();
  task_runner_->PostTask([this] { LoadNextChunk(); });
}

void DB::LoadNextChunk() {
  if (!buf_)
    buf_.reset(new uint8_t[kChunkSize]);

  size_t rsize = reader_->Read(off_, kChunkSize, buf_.get());
  if (rsize == 0)
    return;

  const uint8_t* start = buf_.get();
  const uint8_t* end = start + rsize;
  const uint8_t* buf = start;
  const uint8_t* last_packet = start;

  while (buf < end) {
    if (end - buf < 5)
      break;
    if (*(buf++) != 0x0a) {  // Field ID:1, type:length delimited.
      PERFETTO_ELOG("Trace corrupted @ %" PRIx32,
                    off_ + static_cast<uint32_t>(buf - start));
      exit(1);
    }
    uint64_t packet_size = 0;
    buf = proto_utils::ParseVarInt(buf, end, &packet_size);
    if (buf + packet_size > end)
      break;
    LoadPacket(buf, static_cast<uint32_t>(packet_size));
    buf += packet_size;
    last_packet = buf;
  }
  fflush(stdout);

  off_ += static_cast<uint32_t>(last_packet - start);
  if (rsize < kChunkSize) {
    PERFETTO_CHECK(buf == end);
    perfetto::base::TimeMillis now = perfetto::base::GetWallTimeMs();
    int64_t ms = (now - load_start_time_).count();
    PERFETTO_ILOG("Indexing done (%" PRId32 " KB, %" PRId64 " ms, %.2lf MB/s)",
                  off_ / 1024, ms, off_ * 1000.0 / ms / 1024.0 / 1024.0);
    return;
  }
  task_runner_->PostTask([this] { LoadNextChunk(); });
}

void DB::LoadPacket(const uint8_t* start, size_t size) {
  const uint8_t* end = start + size;
  const uint8_t* cur = start;
  while (cur < end) {
    proto_utils::FieldDescriptor desc{};
    cur = proto_utils::ParseField(cur, end, &desc);
    switch (desc.id) {
      case protos::TracePacket::kFtraceEventsFieldNumber:
        LoadFtraceEventBundle(start + desc.nested.offset, desc.nested.size);
        break;
      default:
        break;
    }
  }
  PERFETTO_CHECK(cur == end);
}

void DB::LoadFtraceEventBundle(const uint8_t* start, size_t size) {
  const uint8_t* end = start + size;
  const uint8_t* cur = start;
  int cpu = -1;

  // Monstrous hack, relies on the writing order of the field in the proto.
  if (size > 4 && end[-4] == 0x08) {
    cpu = static_cast<int>(end[-3]);
  }
  while (cur < end) {
    proto_utils::FieldDescriptor desc{};
    const uint8_t* evt_base = cur;
    cur = proto_utils::ParseField(cur, end, &desc);
    switch (desc.id) {
      case protos::FtraceEventBundle::kCpuFieldNumber:
        PERFETTO_DCHECK(cpu == static_cast<int>(desc.int_value));
        break;
      case protos::FtraceEventBundle::kEventFieldNumber:
        PERFETTO_DCHECK(cpu >= 0);
        LoadFtraceEvent(static_cast<uint32_t>(cpu),
                        evt_base + desc.nested.offset, desc.nested.size);
        break;
      default:
        break;
    }
  }
}

void DB::LoadFtraceEvent(uint32_t cpu, const uint8_t* start, size_t size) {
  const uint8_t* end = start + size;
  const uint8_t* cur = start;
  uint64_t timestamp = 0;

  while (cur < end) {
    proto_utils::FieldDescriptor desc{};
    const uint8_t* evt_base = cur;
    cur = proto_utils::ParseField(cur, end, &desc);
    switch (desc.id) {
      case protos::FtraceEvent::kTimestampFieldNumber:
        timestamp = desc.int_value;
        break;
      case protos::FtraceEvent::kSchedSwitchFieldNumber:
        PERFETTO_DCHECK(timestamp > 0);
        LoadSchedSwitch(cpu, timestamp, evt_base + desc.nested.offset,
                        desc.nested.size);
        break;
      default:
        break;
    }
  }
}

void DB::LoadSchedSwitch(uint32_t cpu,
                         uint64_t timestamp,
                         const uint8_t* start,
                         size_t size) {
  const uint8_t* end = start + size;
  const uint8_t* cur = start;
  uint32_t prev_pid = 0;
  uint32_t next_pid = 0;
  // ThreadName prev_comm{};
  ThreadName next_comm{};
  PERFETTO_DCHECK(cpu < kMaxCpus);
  SchedSlicesPerCpu& cpu_data = cpu_slices_[cpu];

  while (cur < end) {
    proto_utils::FieldDescriptor desc{};
    const uint8_t* evt_base = cur;
    cur = proto_utils::ParseField(cur, end, &desc);
    switch (desc.id) {
      case protos::SchedSwitchFtraceEvent::kPrevPidFieldNumber:
        prev_pid = static_cast<uint32_t>(desc.int_value);
        break;
      case protos::SchedSwitchFtraceEvent::kNextPidFieldNumber:
        next_pid = static_cast<uint32_t>(desc.int_value);
        break;
      // case protos::SchedSwitchFtraceEvent::kPrevCommFieldNumber:
      //   strncpy(&prev_comm[0],
      //           reinterpret_cast<const char*>(evt_base +
      //           desc.nested.offset), std::min(size_t(desc.nested.size),
      //           sizeof(prev_comm) - 1));
      //   break;
      case protos::SchedSwitchFtraceEvent::kNextCommFieldNumber:
        strncpy(&next_comm[0],
                reinterpret_cast<const char*>(evt_base + desc.nested.offset),
                std::min(size_t(desc.nested.size), sizeof(next_comm) - 1));
        break;
      default:
        break;
    }
  }

  uint64_t prev_timestamp = cpu_data.last_timestamp;
  if (prev_timestamp) {
    PERFETTO_DCHECK(timestamp >= prev_timestamp);
    // if(prev_pid != cpu_data.last_tid)
    //  PERFETTO_DLOG("err");
    uint64_t duration = timestamp - prev_timestamp;
    cpu_data.tids.emplace_back(cpu_data.last_tid);
    cpu_data.thread_names.emplace_back(cpu_data.last_tid_name);
    cpu_data.durations.emplace_back(duration);
    cpu_data.timestamps.emplace_back(prev_timestamp);
  }
  cpu_data.last_timestamp = timestamp;
  cpu_data.last_tid = next_pid;
  cpu_data.last_tid_name = InternThreadName(next_comm);
}

// TODO: Assumes no hash collisions.
uint32_t DB::InternThreadName(const ThreadName& name) {
  uint32_t hash = 0;
  for (size_t i = 0; i < sizeof(name); ++i)
    hash = static_cast<uint32_t>(name[i]) + (hash * 31);
  auto it = thread_names_index_.find(hash);
  if (it != thread_names_index_.end())
    return it->second;
  const uint32_t idx = static_cast<uint32_t>(thread_names_.size());
  thread_names_.emplace_back(name);
  thread_names_index_.emplace(hash, idx);
  return idx;
}

// +---------------------------------------------------------------------------+
// | SQLite 3 Virtual Table implementation.                                    |
// +---------------------------------------------------------------------------+

// static
sqlite3_module DB::VirtualTable::spec_{};

DB::VirtualTable::VirtualTable(DB* db) : db_(db) {
  static_assert(offsetof(DB::VirtualTable, base_) == 0, "layout error");
  memset(&base_, 0, sizeof(base_));
}

int DB::VirtualTable::Open(sqlite3_vtab_cursor** cursor) {
  *cursor = reinterpret_cast<sqlite3_vtab_cursor*>(new Cursor(this));
  return SQLITE_OK;
}

int DB::VirtualTable::BestIndex(sqlite3_index_info* idx) {
  bool external_ordering_required = false;
  for (int i = 0; i < idx->nOrderBy; i++) {
    if (idx->aOrderBy[i].iColumn != Cols::kTimestamp || idx->aOrderBy[i].desc)
      external_ordering_required = true;
  }
  idx->orderByConsumed = !external_ordering_required;

  // TODO description of the logic here.
  constraints_.clear();
  int nargs = 0;
  for (int i = 0; i < idx->nConstraint; i++) {
    const auto& cs = idx->aConstraint[i];
    if (cs.usable) {
      constraints_.emplace_back();
      auto* constraint_copy = &constraints_.back();
      memcpy(constraint_copy, &cs, sizeof(cs));
      idx->aConstraintUsage[i].argvIndex = ++nargs;
    }
  }
  return SQLITE_OK;
}

// static
int DB::VirtualTable::Connect(sqlite3* db,
                              void* /*pAux*/,
                              int /*argc*/,
                              const char* const* /*argv*/,
                              sqlite3_vtab** ppVtab,
                              char** /*pzErr*/) {
  int res =
      sqlite3_declare_vtab(db,
                           "CREATE TABLE x(ts, cpu, tid, pid, tname, pname, "
                           "dur, PRIMARY KEY(cpu, ts)) WITHOUT ROWID; ");
  if (res != SQLITE_OK)
    return res;
  *ppVtab = &(new VirtualTable(g_db))->base_;
  return SQLITE_OK;
}

int DB::VirtualTable::Disconnect() {
  delete this;
  return SQLITE_OK;
}

// static
int DB::VirtualTable::InitModule(sqlite3* db,
                                 char** /*pzErrMsg*/,
                                 const sqlite3_api_routines* api) {
  SQLITE_EXTENSION_INIT2(api);
  memset(&spec_, 0, sizeof(spec_));
  spec_.xConnect = &DB::VirtualTable::Connect;
  spec_.xBestIndex = [](sqlite3_vtab* t, sqlite3_index_info* i) {
    return Get(t)->BestIndex(i);
  };
  spec_.xDisconnect = [](sqlite3_vtab* t) { return Get(t)->Disconnect(); };
  spec_.xOpen = [](sqlite3_vtab* t, sqlite3_vtab_cursor** c) {
    return Get(t)->Open(c);
  };
  spec_.xClose = [](sqlite3_vtab_cursor* c) { return GetCursor(c)->Destroy(); };
  spec_.xFilter = [](sqlite3_vtab_cursor* c, int i, const char* s, int a,
                     sqlite3_value** v) {
    return GetCursor(c)->Filter(i, s, a, v);
  };
  spec_.xNext = [](sqlite3_vtab_cursor* c) { return GetCursor(c)->Next(); };
  spec_.xEof = [](sqlite3_vtab_cursor* c) { return GetCursor(c)->Eof(); };
  spec_.xColumn = [](sqlite3_vtab_cursor* c, sqlite3_context* a, int b) {
    return GetCursor(c)->Column(a, b);
  };
  spec_.xRowid = [](sqlite3_vtab_cursor*, sqlite_int64*) {
    return SQLITE_ERROR;
  };
  return sqlite3_create_module(db, "trace", &spec_, 0);
}

DB::Cursor::Cursor(DB::VirtualTable* table) : table_(table), db_(table->db()) {
  static_assert(offsetof(DB::Cursor, base_) == 0, "layout error");
  memset(&base_, 0, sizeof(base_));
}

int DB::Cursor::Destroy() {
  delete this;
  return SQLITE_OK;
}

void DB::Cursor::Reset() {
  cpu_mask_ = uint64_t(-1);
  cur_cpu_ = 0;
  cur_cpu_event_ = 0;
  last_timestamp_ = 0;
  for (uint32_t& next_row_for_cpu : next_row_)
    next_row_for_cpu = 0;
  for (uint32_t cpu = 0; cpu < DB::kMaxCpus; cpu++) {
    SchedSlicesPerCpu* slices = db_->slices_for_cpu(cpu);
    row_masks_[cpu].assign(slices->num_rows(), true);
    row_masks_[cpu].resize(slices->num_rows());
  }
}

// This method is called to "rewind" the VtabCursor object back
// to the first row of output.  This method is always called at least
// once prior to any call to Column() or RowId() or Eof().
int DB::Cursor::Filter(int /*idxNum*/,
                       const char* /*idxStr*/,
                       int argc,
                       sqlite3_value** argv) {
  Reset();

  PERFETTO_CHECK(static_cast<size_t>(argc) == table_->constraints().size());
  uint64_t ts_min = 0;
  uint64_t ts_max = uint64_t(-1);
  size_t i = size_t(-1);
  for (const auto& cs : table_->constraints()) {
    i++;
    bool constraint_implemented = false;
    if (cs.iColumn == Cols::kCpu) {
      if (cs.op == SQLITE_INDEX_CONSTRAINT_EQ) {
        cpu_mask_ &= (1ull << sqlite3_value_int64(argv[i]));
        constraint_implemented = true;
      }
    } else if (cs.iColumn == Cols::kTimestamp) {
      if (cs.op == SQLITE_INDEX_CONSTRAINT_GE ||
          cs.op == SQLITE_INDEX_CONSTRAINT_GT) {
        ts_min = static_cast<uint64_t>(sqlite3_value_int64(argv[i]));
        constraint_implemented = true;
      } else if (cs.op == SQLITE_INDEX_CONSTRAINT_LE ||
                 cs.op == SQLITE_INDEX_CONSTRAINT_LT) {
        ts_max = static_cast<uint64_t>(sqlite3_value_int64(argv[i]));
        constraint_implemented = true;
      }
    }

    if (!constraint_implemented) {
      PERFETTO_ELOG(
          "Constraint: col:%d op:%d not implemented, life is too short",
          cs.iColumn, cs.op);
      return SQLITE_ERROR;
    }
  }

  // If the query specified a filter on the timestamp, mask out events in the
  // bitmap that are out of bounds. While doing this, also set the |next_evt|
  // to the first valid event.
  for (uint32_t cpu = 0; cpu < kMaxCpus; cpu++) {
    if (!((1ull << cpu) & cpu_mask_))
      continue;
    if (ts_min == 0 && ts_max == uint64_t(-1))
      continue;
    SchedSlicesPerCpu* cpu_slices = db_->slices_for_cpu(cpu);
    bool did_set_next_evt = false;
    for (uint32_t evt = 0; evt < cpu_slices->num_rows(); evt++) {
      auto ts = cpu_slices->timestamps[evt];
      if (ts < ts_min || ts > ts_max)
        row_masks_[cpu][evt] = false;
      else if (!did_set_next_evt) {
        next_row_[cpu] = evt;
        did_set_next_evt = true;
      }
    }
  }

  return SQLITE_OK;
}

int DB::Cursor::Next() {
  // Find the next event in timestmp order across all cpus.
  uint64_t min_ts = uint64_t(-1);
  uint32_t min_cpu = 0xff;
  uint32_t cpu = 0;
  for (uint64_t mask = 1; mask; mask <<= 1ull, cpu++) {
    if (!(cpu_mask_ & mask))
      continue;
    SchedSlicesPerCpu* cpu_data = db_->slices_for_cpu(cpu);
    if (next_row_[cpu] >= cpu_data->num_rows())
      continue;
    uint64_t ts = cpu_data->timestamps[next_row_[cpu]];
    if (ts < min_ts) {
      min_cpu = cpu;
      min_ts = ts;
    }
  }

  PERFETTO_DCHECK(min_cpu < kMaxCpus);
  cpu = cur_cpu_ = min_cpu;
  cur_cpu_event_ = next_row_[cpu];

  // Now move the per-cpu cursor to the next event (if any).
  SchedSlicesPerCpu* cpu_data = db_->slices_for_cpu(cpu);
  auto* next_evt = &next_row_[cpu];
  for ((*next_evt)++; *next_evt < cpu_data->num_rows(); (*next_evt)++) {
    if (row_masks_[cpu][*next_evt])
      break;
  }

  return SQLITE_OK;
}

int DB::Cursor::Eof() {
  uint32_t cpu = 0;
  for (uint64_t mask = 1; mask; mask <<= 1ull, cpu++) {
    if (!(cpu_mask_ & mask))
      continue;
    SchedSlicesPerCpu* cpu_data = db_->slices_for_cpu(cpu);
    if (next_row_[cpu] >= cpu_data->num_rows())
      continue;
    return 0;
  }
  return 1;
}

int DB::Cursor::Column(sqlite3_context* ctx, int col) {
  SchedSlicesPerCpu* cpu_data = db_->slices_for_cpu(cur_cpu_);

  switch (col) {
    case Cols::kTimestamp:
      sqlite3_result_int64(
          ctx, static_cast<int64_t>(cpu_data->timestamps[cur_cpu_event_]));
      break;
    case Cols::kDuration:
      sqlite3_result_int64(
          ctx, static_cast<int64_t>(cpu_data->durations[cur_cpu_event_]));
      break;
    case Cols::kCpu:
      sqlite3_result_int(ctx, static_cast<int>(cur_cpu_));
      break;
    case Cols::kTid:
      sqlite3_result_int(ctx, static_cast<int>(cpu_data->tids[cur_cpu_event_]));
      break;
    case Cols::kPid:
      sqlite3_result_int(ctx, 0);
      break;
    case Cols::kTname: {
      uint32_t name_idx = cpu_data->thread_names[cur_cpu_event_];
      const ThreadName& tname = db_->thread_names_[name_idx];
      sqlite3_result_text(ctx, &tname[0], sizeof(tname), nullptr);
    } break;
    case Cols::kPname:
      sqlite3_result_null(ctx);
      break;
    default:
      sqlite3_result_null(ctx);
      break;
  }
  return SQLITE_OK;
}

}  // namespace trace_processor
}  // namespace perfetto
