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

#include <aio.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <sstream>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "perfetto/base/scoped_file.h"

#include "perfetto/trace_processor/raw_query.pb.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_MACOSX)
#define PERFETTO_HAS_SIGNAL_H() 1
#else
#define PERFETTO_HAS_SIGNAL_H() 0
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_STANDALONE_BUILD)
#include <pwd.h>
#include <sys/types.h>
#endif

#if PERFETTO_HAS_SIGNAL_H()
#include <signal.h>
#endif

namespace perfetto {
namespace trace_processor {

namespace {
TraceProcessor* g_tp;

#if PERFETTO_BUILDFLAG(PERFETTO_STANDALONE_BUILD)

#else

void SetupLineEditor() {}

void FreeLine(char* line) {
  delete[] line;
}

char* GetLine(const char* prompt) {
  printf("\r%80s\r%s", "", prompt);
  fflush(stdout);
  char* line = new char[1024];
  if (!fgets(line, 1024 - 1, stdin)) {
    FreeLine(line);
    return nullptr;
  }
  if (strlen(line) > 0)
    line[strlen(line) - 1] = 0;
  return line;
}

#endif

void PrintUsage(char** argv) {
  PERFETTO_ELOG("Usage: %s [-d] [-q query.sql] trace_file.pb", argv[0]);
}

// Convenience function to avoid the callback boilerplate.
protos::RawQueryResult ExecuteQuerySync(
    TraceProcessor* tp,  std::string query) {
  protos::RawQueryArgs args;
  args.set_sql_query(query);
  protos::RawQueryResult res;
  // std::cout << "Running query: " << query << std::endl;
  tp->ExecuteQuery(args, [&res](const protos::RawQueryResult& cb_res) {
    res = cb_res;
  });
  if (res.has_error()) {
    PERFETTO_ELOG("SQLite error: %s", res.error().c_str());
  }
  PERFETTO_CHECK(res.columns_size() == res.column_descriptors_size());
  return res;
}

// NOTE: tp can currenty be accessed directly through g_tp, but pretending
// g_tp won't be eventually available.
void ComputeTTI(TraceProcessor* tp) {
  std::ostringstream nav_q_str;
  nav_q_str << "select ts, utid from slices where name = \"navigationStart\""
    << "and json_extract(args, \"$.data.isLoadingMainFrame\") = 1";
  auto nav_start_q = ExecuteQuerySync(tp, nav_q_str.str());
  if (nav_start_q.num_records() == 0)
    PERFETTO_FATAL("No records found");
  for (unsigned int i = 0; i < nav_start_q.num_records(); i++) {
    int64_t utid = nav_start_q.columns(1).long_values(static_cast<int>(i));
    int64_t cur_nav_start =
        nav_start_q.columns(0).long_values(static_cast<int>(i));
    int64_t next_nav_start =
        i + 1 == nav_start_q.num_records()
            ? -1
            : nav_start_q.columns(0).long_values(static_cast<int>(i + 1));

    std::ostringstream dcl_q_str;
    dcl_q_str
        << "select ts from slices where name = \"domContentLoadedEventEnd\""
        << " and ts > " << cur_nav_start;
    if (next_nav_start != -1) {
      dcl_q_str << " and ts < " << next_nav_start;
    }

    // TODO: Instead of iterating through all the slices multiple time, can we
    // come up with some kind of filter-on-a-stream approach?
    auto dcl_q = ExecuteQuerySync(tp, dcl_q_str.str());
    int64_t dcl;
    if (dcl_q.num_records() == 0)
      dcl = -1;
    else
      dcl = dcl_q.columns(0).long_values(0);

    std::ostringstream fcp_q_str;
    fcp_q_str << "select ts from slices where name = \"firstContentfulPaint\""
              << " and ts > " << cur_nav_start;
    if (next_nav_start != -1) {
      fcp_q_str << " and ts < " << next_nav_start;
    }
    auto fcp_q = ExecuteQuerySync(tp, fcp_q_str.str());
    int64_t fcp;
    if (fcp_q.num_records() == 0)
      fcp = -1;
    else
      fcp = fcp_q.columns(0).long_values(0);

    std::ostringstream long_tasks_q_str;
    long_tasks_q_str << "select ts, dur from slices where dur > "
                     << 50 * 1000000 << " and utid = " << utid << " and ts > "
                     << fcp;
    if (next_nav_start != -1) {
      long_tasks_q_str << " and ts < " << next_nav_start;
    }

    auto long_tasks_q = ExecuteQuerySync(tp, long_tasks_q_str.str());
    uint64_t long_tasks_count = long_tasks_q.num_records();

    if (fcp == -1) continue;
    std::cout << "Navigation " << i << std::endl;
    std::cout << "utid: " << utid << std::endl;
    std::cout << "Nav start: " << cur_nav_start << std::endl;
    std::cout << "DCLEnd: " << dcl << std::endl;
    std::cout << "FCP: " << fcp << std::endl;
    std::cout << "Long tasks: " << long_tasks_count << std::endl;


    int64_t tti;
    if (long_tasks_q.num_records() == 0) {
      uint64_t last_index = long_tasks_q.num_records() - 1;

      int64_t long_task_end =
        static_cast<int64_t>(
          long_tasks_q.columns(0).long_values(static_cast<int>(last_index))) +
        long_tasks_q.columns(1).long_values(static_cast<int>(last_index));

      tti = std::max(long_task_end, dcl);
    } else {
      tti = std::max(fcp, dcl);
    }

    std::cout << "TTI: " << tti << std::endl;
  }
}

int RunMetricMain(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage(argv);
    return 1;
  }
  const char* trace_file_path = nullptr;
  const char* query_file_path = nullptr;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0) {
      EnableSQLiteVtableDebugging();
      continue;
    }
    if (strcmp(argv[i], "-q") == 0) {
      if (++i == argc) {
        PrintUsage(argv);
        return 1;
      }
      query_file_path = argv[i];
      continue;
    }
    trace_file_path = argv[i];
  }

  if (trace_file_path == nullptr) {
    PrintUsage(argv);
    return 1;
  }

  // Load the trace file into the trace processor.
  Config config;
  config.optimization_mode = OptimizationMode::kMaxBandwidth;
  TraceProcessor tp(config);
  base::ScopedFile fd(base::OpenFile(trace_file_path, O_RDONLY));
  PERFETTO_CHECK(fd);

  // Load the trace in chunks using async IO. We create a simple pipeline where,
  // at each iteration, we parse the current chunk and asynchronously start
  // reading the next chunk.

  // 1MB chunk size seems the best tradeoff on a MacBook Pro 2013 - i7 2.8 GHz.
  constexpr size_t kChunkSize = 1024 * 1024;
  struct aiocb cb {};
  cb.aio_nbytes = kChunkSize;
  cb.aio_fildes = *fd;

  std::unique_ptr<uint8_t[]> aio_buf(new uint8_t[kChunkSize]);
  cb.aio_buf = aio_buf.get();

  PERFETTO_CHECK(aio_read(&cb) == 0);
  struct aiocb* aio_list[1] = {&cb};

  uint64_t file_size = 0;
  auto t_load_start = base::GetWallTimeMs();
  for (int i = 0;; i++) {
    if (i % 128 == 0)
      fprintf(stderr, "\rLoading trace: %.2f MB\r", file_size / 1E6);

    // Block waiting for the pending read to complete.
    PERFETTO_CHECK(aio_suspend(aio_list, 1, nullptr) == 0);
    auto rsize = aio_return(&cb);
    if (rsize <= 0)
      break;
    file_size += static_cast<uint64_t>(rsize);

    // Take ownership of the completed buffer and enqueue a new async read
    // with a fresh buffer.
    std::unique_ptr<uint8_t[]> buf(std::move(aio_buf));
    aio_buf.reset(new uint8_t[kChunkSize]);
    cb.aio_buf = aio_buf.get();
    cb.aio_offset += rsize;
    PERFETTO_CHECK(aio_read(&cb) == 0);

    // Parse the completed buffer while the async read is in-flight.
    tp.Parse(std::move(buf), static_cast<size_t>(rsize));
  }
  tp.NotifyEndOfFile();
  double t_load = (base::GetWallTimeMs() - t_load_start).count() / 1E3;
  double size_mb = file_size / 1E6;
  PERFETTO_ILOG("Trace loaded: %.2f MB (%.1f MB/s)", size_mb, size_mb / t_load);
  g_tp = &tp;

#if PERFETTO_HAS_SIGNAL_H()
  signal(SIGINT, [](int) { g_tp->InterruptQuery(); });
#endif

  ComputeTTI(g_tp);
  return 0;
}

}  // namespace

}  // namespace trace_processor
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::trace_processor::RunMetricMain(argc, argv);
}
