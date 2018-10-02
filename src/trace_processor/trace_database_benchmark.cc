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

#include <map>
#include <random>
#include <string>

#include "benchmark/benchmark.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/trace_processor/raw_query.pb.h"
#include "src/base/test/utils.h"
#include "src/trace_processor/json_trace_parser.h"
#include "src/trace_processor/trace_processor.h"

namespace perfetto {
namespace trace_processor {
namespace {

constexpr size_t kChunkSize = 128 * 1024;

class TraceProcessorFixture {
 public:
  TraceProcessorFixture() : processor(TraceProcessor::Config()) {}

  TraceProcessor processor;

  bool LoadTrace(const char* name) {
    base::ScopedFstream f(fopen(base::GetTestDataPath(name).c_str(), "rb"));
    while (!feof(*f)) {
      std::unique_ptr<uint8_t[]> buf(new uint8_t[kChunkSize]);
      auto rsize = fread(reinterpret_cast<char*>(buf.get()), 1, kChunkSize, *f);
      if (!processor.Parse(std::move(buf), rsize))
        return false;
    }
    processor.NotifyEndOfFile();
    return true;
  }

  void Query(const std::string& query, protos::RawQueryResult* result) {
    protos::RawQueryArgs args;
    args.set_sql_query(query);
    auto on_result = [&result](const protos::RawQueryResult& res) {
      result->CopyFrom(res);
    };
    processor.ExecuteQuery(args, on_result);
  }
};

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto

using perfetto::trace_processor::TraceProcessorFixture;
using perfetto::protos::RawQueryResult;

static void BM_TraceProcessor_LoadProtoTrace(benchmark::State& state) {
  while (state.KeepRunning()) {
    TraceProcessorFixture fixture;
    fixture.LoadTrace("android_sched_and_ps.pb");
  }
}
BENCHMARK(BM_TraceProcessor_LoadProtoTrace);

static void BM_TraceProcessor_SchedQuery(benchmark::State& state) {
  TraceProcessorFixture fixture;
  fixture.LoadTrace("android_sched_and_ps.pb");
  RawQueryResult res;
  while (state.KeepRunning()) {
    fixture.Query("select ts, dur from sched where cpu = 0 and ts > 1e9", &res);
  }
}
BENCHMARK(BM_TraceProcessor_SchedQuery);

static void BM_TraceProcessor_LoadJsonTrace(benchmark::State& state) {
  while (state.KeepRunning()) {
    TraceProcessorFixture fixture;
    fixture.LoadTrace("sfgate.json");
  }
}
BENCHMARK(BM_TraceProcessor_LoadJsonTrace);
