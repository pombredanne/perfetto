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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_DB_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_DB_H_

#include <stdint.h>

#include <array>
#include <deque>
#include <unordered_map>
#include <vector>

#include "perfetto/base/time.h"

#include "sqlite3ext.h"

extern "C" struct sqlite3;

namespace perfetto {

namespace base {
class TaskRunner;
}

namespace trace_processor {

class BlobReader;

// This implements the RPC methods defined in raw_query.proto.
class DB {
 public:
  // Columnar storage of sched intervals for each CPU.
  struct SchedSlicesPerCpu {
    // All deques below have the same size(), which is == num rows.
    std::deque<uint32_t> tids;
    std::deque<uint32_t> thread_names;
    std::deque<uint64_t> timestamps;
    std::deque<uint64_t> durations;

    uint32_t num_rows() const { return static_cast<uint32_t>(tids.size()); }

    // Used to convert events into intervals when populating.
    uint32_t last_tid = 0;
    uint32_t last_tid_name = 0;
    uint64_t last_timestamp = 0;
  };

  explicit DB(base::TaskRunner*);
  void LoadTrace(BlobReader*);
  void Query(const char* query);

  SchedSlicesPerCpu* slices_for_cpu(uint32_t cpu) { return &cpu_slices_[cpu]; }

 private:
  static constexpr uint32_t kMaxCpus = 64;
  using ThreadName = std::array<char, 16>;

  enum Cols : int {
    kTimestamp = 0,
    kCpu = 1,
    kTid = 2,
    kPid = 3,
    kTname = 4,
    kPname = 5,
    kDuration = 6
  };

  class Cursor;

  class VirtualTable {
   public:
    using Constraint = sqlite3_index_info::sqlite3_index_constraint;
    explicit VirtualTable(DB*);

    static VirtualTable* Get(sqlite3_vtab* base) {
      return reinterpret_cast<VirtualTable*>(base);
    }

    static Cursor* GetCursor(sqlite3_vtab_cursor* base) {
      return reinterpret_cast<Cursor*>(base);
    }

    static int Connect(sqlite3*,
                       void*,
                       int,
                       const char* const*,
                       sqlite3_vtab**,
                       char**);

    int Disconnect();
    int Open(sqlite3_vtab_cursor**);
    int Close();
    int VtabNext(sqlite3_vtab_cursor*);
    int BestIndex(sqlite3_index_info*);

    static int InitModule(sqlite3*, char**, const sqlite3_api_routines*);

    DB* db() const { return db_; }
    const std::vector<Constraint>& constraints() const { return constraints_; }

   private:
    static sqlite3_module spec_;

    sqlite3_vtab base_;  // Must be first.
    DB* const db_;

    // Updated by the BestIndex() method.
    std::vector<Constraint> constraints_;
  };

  class Cursor {
   public:
    explicit Cursor(VirtualTable*);

    int Destroy();
    int Filter(int idxNum, const char* idxStr, int argc, sqlite3_value** argv);
    int Column(sqlite3_context*, int col);
    int Next();
    int Eof();

    void Reset();

   private:
    Cursor(const Cursor&) = delete;
    Cursor& operator=(const Cursor&) = delete;

    sqlite3_vtab_cursor base_;  // Must be first.
    VirtualTable* const table_;
    DB* const db_;
    uint64_t cpu_mask_;
    uint32_t cur_cpu_;
    uint32_t cur_cpu_event_;
    uint64_t last_timestamp_;

    // Row masks, one for each cpu.
    // true := row should be emitted by Next(); false := skip.
    std::array<std::vector<bool>, kMaxCpus> row_masks_;
    std::array<uint32_t, kMaxCpus> next_row_;
  };

  DB(const DB&) = delete;
  DB& operator=(const DB&) = delete;

  void LoadNextChunk();
  void LoadPacket(const uint8_t* start, size_t size);
  void LoadFtraceEventBundle(const uint8_t* start, size_t size);
  void LoadFtraceEvent(uint32_t cpu, const uint8_t* start, size_t size);
  void LoadSchedSwitch(uint32_t cpu,
                       uint64_t timestamp,
                       const uint8_t* start,
                       size_t size);
  uint32_t InternThreadName(const ThreadName& name);

  base::TaskRunner* const task_runner_;
  BlobReader* reader_ = nullptr;
  sqlite3* db_ = nullptr;
  std::unique_ptr<uint8_t[]> buf_;
  perfetto::base::TimeMillis load_start_time_{};
  uint32_t off_ = 0;

  SchedSlicesPerCpu cpu_slices_[kMaxCpus];

  // Interned thread names.
  std::deque<ThreadName> thread_names_;

  // hash(name) -> (index in thread_names_).
  std::unordered_map<uint32_t, uint32_t> thread_names_index_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_DB_H_
