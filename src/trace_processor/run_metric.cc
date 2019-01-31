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
#include <algorithm>
#include <vector>
#include <unordered_map>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/time.h"
#include "perfetto/trace_processor/trace_processor.h"

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

using ::google::protobuf::int64;

void PrintUsage(char** argv) {
  PERFETTO_ELOG("Usage: %s trace_file.json", argv[0]);
}

/********************************************************************
 * First some convenience function to avoid boilerplate. These will
 * generally be part of a better written library. None of this is
 * specific to TTI.
 * *****************************************************************/

// This converts the callback based ExecuteQuery API of the trace processor
// to a return value based API. Makes writing and reading code simpler.
protos::RawQueryResult ExecuteQuerySync(TraceProcessor* tp, std::string query) {
  protos::RawQueryArgs args;
  args.set_sql_query(query);
  protos::RawQueryResult res;
  tp->ExecuteQuery(
      args, [&res](const protos::RawQueryResult& cb_res) {
        // Not sure if this std::move has any effect.
        res = std::move(cb_res); });
  if (res.has_error()) {
    PERFETTO_ELOG("SQLite error: %s", res.error().c_str());
    PERFETTO_ELOG("Query: %s", query.c_str());
  }
  PERFETTO_CHECK(res.columns_size() == res.column_descriptors_size());
  return res;
}


// Returns the ColumnValues for column |col_name|. Crashes if |col_name| not
// found.
const protos::RawQueryResult_ColumnValues& GetColumn(const protos::RawQueryResult& res,
                                              const std::string& col_name) {
  for (int i = 0; i < res.column_descriptors_size(); i++) {
    if (res.column_descriptors(i).name() == col_name) {
      return res.columns(i);
    }
  }

  // Column not found.
  PERFETTO_ELOG("Column not found %s", col_name.c_str());
  PERFETTO_IMMEDIATE_CRASH();
}

void PrintQueryResultAsCsv(const protos::RawQueryResult& res, FILE* output) {
  PERFETTO_CHECK(res.columns_size() == res.column_descriptors_size());

  for (int r = 0; r < static_cast<int>(res.num_records()); r++) {
    if (r == 0) {
      for (int c = 0; c < res.column_descriptors_size(); c++) {
        const auto& col = res.column_descriptors(c);
        if (c > 0)
          fprintf(output, ",");
        fprintf(output, "\"%s\"", col.name().c_str());
      }
      fprintf(output, "\n");
    }

    for (int c = 0; c < res.columns_size(); c++) {
      if (c > 0)
        fprintf(output, ",");
      switch (res.column_descriptors(c).type()) {
        case protos::RawQueryResult_ColumnDesc_Type_STRING:
          fprintf(output, "\"%s\"", res.columns(c).string_values(r).c_str());
          break;
        case protos::RawQueryResult_ColumnDesc_Type_DOUBLE:
          fprintf(output, "%f", res.columns(c).double_values(r));
          break;
        case protos::RawQueryResult_ColumnDesc_Type_LONG: {
          auto value = res.columns(c).long_values(r);
          fprintf(output, "%lld", value);
          break;
        }
      }
    }
    printf("\n");
  }
}

// Thin wrapper around RawQueryResult to make accessing columns and printing
// slightly easier.
struct QueryResult {
  QueryResult(protos::RawQueryResult&& res) : result(std::move(res)) {}
  const protos::RawQueryResult result;
  const protos::RawQueryResult_ColumnValues& Column(const std::string& col_name) const{
    return GetColumn(result, col_name);
  }
  int num_records() const {
    return static_cast<int>(result.num_records());
  }
  void Print() const {
    PrintQueryResultAsCsv(result, stdout);
  }
};

// Thin wrapper around TraceProcessor to make querying slighly easier.
struct TraceProcessorWrapper {
  TraceProcessorWrapper(TraceProcessor* tp) : tp_(tp) {}
  QueryResult Query(const std::string& query) const {
    return QueryResult((ExecuteQuerySync(tp_, query)));
  }

  // This function is here because it's quick and easy to replace the string
  // 'Query' witih 'LogAndQuery' :)
  QueryResult LogAndQuery(const std::string& query) const {
    PERFETTO_ELOG("Executing query: %s", query.c_str());
    return Query(query);
  }

  TraceProcessor* tp_;
};

/*********************************************************************
 * TTI Metric code starts from here. We have a few helper function
 * and data structures, and then the main metric function ComputeTTI.
 ********************************************************************/
static const int ACTIVE_REQUEST_TOLERANCE = 2;
static constexpr int64 TTI_WINDOW_SIZE_NS = 5L * 1000L * 1000L * 1000L;

// A timestamp, tagged with what the timestamp represents.
struct Endpoint {
  enum EndpointType {
    TaskStart,
    TaskEnd,
    LoadStart,
    LoadEnd,
    NavigationEnd,
  };
  Endpoint(EndpointType type_, int64 ts_) : type(type_), ts(ts_) {}
  EndpointType type;
  int64 ts;
};

// Given a column |col| of timestamps, and a tag |type|, putted all the
// tagged timestamps ("endpoint"s) in the |endpoints| vector.
void ExtractEndpoints(std::vector<Endpoint>& endpoints,
    const protos::RawQueryResult_ColumnValues& col,
    Endpoint::EndpointType type) {
  for (auto ts: col.long_values()) {
    endpoints.emplace_back(type, ts);
  }
}

// Returns true iff sufficiently long both main thread and network quiet
// windows have been found.
bool ReachedQuiescence(int64 mt_quiet_window_start,
    int64 net_quiet_window_start, int64 curr_ts) {
  if (mt_quiet_window_start == -1 || net_quiet_window_start == -1) {
    // Currently not in a quiet window.
    return false;
  }

  return (curr_ts - mt_quiet_window_start > TTI_WINDOW_SIZE_NS &&
      curr_ts - net_quiet_window_start > TTI_WINDOW_SIZE_NS);
}

// Returns the max ts of event in nav_range with given id and frame.
// Assumes frame is available in args in the from args = {frame: string}.
// Returns -1 if event not found.
int64 GetMaxEventTs(const TraceProcessorWrapper& tpw,
                    const std::pair<int64, int64>& nav_range,
                    int64 utid, const std::string& frame,
                    const std::string& slice_name) {
  auto query = tpw.Query((std::ostringstream()
    << "select max(ts) as event_ts from slices"
    << " where name = '" << slice_name << "'"
    << " and ts > " << nav_range.first
    << " and ts < " << nav_range.second
    << " and utid = " << utid
    << " and json_extract(args, \"$.frame\") = '" << frame << "'"
    << " group by ts")  // Otherwise we get a null row when no event_ts.
  .str());
  return query.num_records() > 0 ?
    query.Column("event_ts").long_values(0) : -1;
}

typedef std::unordered_map<std::string, std::vector<int64>> StringToIntVector;

// Return a map of frame to all the navigationStart timestamps of that frame.
StringToIntVector GetFrameToNavs(const TraceProcessorWrapper& tpw, int64 utid) {
  StringToIntVector frame_to_nav;

  auto main_frame_navs = tpw.Query((std::ostringstream()
    << "select ts, dur, json_extract(args, \"$.frame\") as frame from slices"
    << " where utid = " << utid
    << " and name = \"navigationStart\""
    << " and json_extract(args, \"$.data.isLoadingMainFrame\")"
    << " order by ts")
  .str());

  for (int i = 0; i < main_frame_navs.num_records(); i++) {
    frame_to_nav[main_frame_navs.Column("frame").string_values(i)]
      .push_back(main_frame_navs.Column("ts").long_values(i));
  }

  return frame_to_nav;
}

// Iterates over the tagged timestamps (endpoints) and calculates TTI.
// Returns -1 if TTI was not reached.
int64 GetInteractiveCandidate(const QueryResult& long_tasks,
    const QueryResult& resource_loads, int64 fcp, int64 nav_end) {
  // Extract timestamps where a quiet window can possibly change.
  // We extract start and end of long tasks and resource loads, and the
  // end of navigation event.
  std::vector<Endpoint> endpoints;
  ExtractEndpoints(endpoints,
      long_tasks.Column("task_start"),
      Endpoint::TaskStart);
  ExtractEndpoints(endpoints,
      long_tasks.Column("task_end"),
      Endpoint::TaskEnd);
  ExtractEndpoints(endpoints,
      resource_loads.Column("load_start"),
      Endpoint::LoadStart);
  ExtractEndpoints(endpoints,
      resource_loads.Column("load_end"),
      Endpoint::LoadEnd);
  endpoints.emplace_back(Endpoint::NavigationEnd, nav_end);

  int64 net_quiet_window_start = fcp;
  int64 mt_quiet_window_start = fcp;
  int num_active_requests = 0;
  int64 interactive_candidate = -1;

  std::sort(endpoints.begin(), endpoints.end(),
    [](const Endpoint& lhs, const Endpoint& rhs) { return lhs.ts < rhs.ts;});

  for (auto& endpoint: endpoints) {
    if (ReachedQuiescence(mt_quiet_window_start,
          net_quiet_window_start, endpoint.ts)) {
      interactive_candidate = mt_quiet_window_start;
      break;
    }

    switch (endpoint.type) {
      case Endpoint::TaskStart: {
        mt_quiet_window_start = -1;
        break;
      }
      case Endpoint::TaskEnd: {
        mt_quiet_window_start = endpoint.ts;
        break;
      }
      case Endpoint::LoadStart: {
        num_active_requests++;
        if (num_active_requests > ACTIVE_REQUEST_TOLERANCE) {
          net_quiet_window_start = -1;
        }
        break;
      }
      case Endpoint::LoadEnd: {
        num_active_requests--;
        if (num_active_requests == ACTIVE_REQUEST_TOLERANCE) {
          // Just became network quiet.
          net_quiet_window_start = endpoint.ts;
        }
        break;
      }
      case Endpoint::NavigationEnd: {
        // Do nothing.
        break;
      }
    }
  }

  return interactive_candidate;
}

void ComputeTTI(TraceProcessor* tp) {
  TraceProcessorWrapper tpw(tp);

  // This metric table will be populated.
  tpw.Query(
    "create table tti_metric(nav_start, nav_end, upid, frame, TTI)"
  );

  // Get trace bounds. Ideally this table will already be created.
  auto traceBounds = tpw.Query(
      "select min(ts) as traceStart, max(ts + dur) as traceEnd from"
      "(select ts, dur from slices "
      "union all select ts, dur from async_slices);");

  int64 traceEnd = traceBounds.Column("traceEnd").long_values(0);

  auto renderers_query = tpw.Query(
    "select upid from process where name = \'Renderer\'");

  for (int64 upid: renderers_query.Column("upid").long_values()) {
    int64 utid = tpw.Query((std::ostringstream()
      << "select utid from thread where upid = " << upid
      << " and tid = 1"
    ).str()).Column("utid").long_values(0);

    StringToIntVector frame_to_nav = GetFrameToNavs(tpw, utid);

    for (auto& frame_navs: frame_to_nav) {
      const std::string& frame = frame_navs.first;
      std::vector<std::pair<int64, int64>> nav_ranges;

      const std::vector<int64>& navs = frame_navs.second;
      for (size_t i = 0; i < navs.size() - 1; i++) {
        nav_ranges.emplace_back(navs[i], navs[i + 1]);
      }
      nav_ranges.emplace_back(navs[navs.size() - 1], traceEnd);

      for (auto& nav_range: nav_ranges) {
        // First, get DomContentLoadedEnd and FirstContentfulPaint events.
        // Ideally there should be only one DCL event in nav_range, but there
        // are edge cases where there can be more. We take the max to keep
        // things simple.
        int64 dcl = GetMaxEventTs(tpw, nav_range, utid, frame, "domContentLoadedEventEnd");
        if (dcl == -1) continue;  // Cannot compute TTI without DCL.
        int64 fcp = GetMaxEventTs(tpw, nav_range, utid, frame, "firstContentfulPaint");
        if (fcp == -1) continue;  // Cannot compute TTI without FCP.

        // Get all the long tasks we care about.
        auto long_tasks_query = tpw.Query((std::ostringstream()
          << "select ts as task_start, ts + dur as task_end from slices"
          << " where name in ('ThreadControllerImpl::RunTask',"
          << "   'ThreadControllerImpl::DoWork',"
          << "   'TaskQueueManager::ProcessWorkFromTaskQueue')"
          << " and cat = 'toplevel'"
          << " and dur > 50000000"  // Tasks > 50ms = Long tasks.
          << " and utid = " << utid
          << " and task_start < " << nav_range.second
          << " and task_end > " << fcp  // Only care about tasks after FCP.
        ).str());

        // Get all the resource loads we care about.
        auto resource_load_query = tpw.Query((std::ostringstream()
          << "select ts as load_start, ts + dur as load_end from async_slices"
          << " where name = 'ResourceLoad'"
          << " and upid = " << upid
          << " and load_start < " << nav_range.second
          // Need network requests before FCP as well since we need to count
          // # of in flight requests at each timestamp of interest.
          << " and load_end > " << nav_range.first
        ).str());

        int64 interactive_candidate = GetInteractiveCandidate(
          long_tasks_query, resource_load_query, fcp, nav_range.second);

        if (interactive_candidate == -1) continue;  // TTI not found.

        const int64 tti = std::max(dcl, interactive_candidate) - nav_range.first;

        // Insert the computed TTI value into the metrics table.
        tpw.Query((std::ostringstream()
          << "insert into tti_metric values ("
          << nav_range.first << ", "
          << nav_range.second << ", "
          << upid << ", "
          << "'" << frame << "', "
          << tti << ")"
        ).str());
      }
    }
  }

  tpw.LogAndQuery("select * from tti_metric").Print();
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

  // 1MB chunk size seems the best tradeoff on a MacBook Pro 20: - i7 2.8 GHz.
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

  /////////////////////////////////////////////
  // Call to compute metric.
  ComputeTTI(g_tp);
  /////////////////////////////////////////////

  return 0;
}

}  // namespace

}  // namespace trace_processor
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::trace_processor::RunMetricMain(argc, argv);
}
