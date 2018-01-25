// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "benchmark/benchmark.h"

#include "cpu_reader.h"

#include "perfetto/protozero/scattered_stream_writer.h"

#include "perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "test/cpu_reader_support.h"
#include "test/scattered_stream_null_delegate.h"

namespace {

const size_t kPageSize = 4096;

perfetto::ExamplePage g_six_sched_switch{
    "synthetic",
    R"(
    00000000: 2b16 c3be 90b6 0300 a001 0000 0000 0000  +...............
    00000010: 1e00 0000 0000 0000 1000 0000 2f00 0103  ............/...
    00000020: 0300 0000 6b73 6f66 7469 7271 642f 3000  ....ksoftirqd/0.
    00000030: 0000 0000 0300 0000 7800 0000 0100 0000  ........x.......
    00000040: 0000 0000 736c 6565 7000 722f 3000 0000  ....sleep.r/0...
    00000050: 0000 0000 950e 0000 7800 0000 b072 8805  ........x....r..
    00000060: 2f00 0103 950e 0000 736c 6565 7000 722f  /.......sleep.r/
    00000070: 3000 0000 0000 0000 950e 0000 7800 0000  0...........x...
    00000080: 0008 0000 0000 0000 7263 756f 702f 3000  ........rcuop/0.
    00000090: 0000 0000 0000 0000 0a00 0000 7800 0000  ............x...
    000000a0: f0b0 4700 2f00 0103 0700 0000 7263 755f  ..G./.......rcu_
    000000b0: 7072 6565 6d70 7400 0000 0000 0700 0000  preempt.........
    000000c0: 7800 0000 0100 0000 0000 0000 736c 6565  x...........slee
    000000d0: 7000 722f 3000 0000 0000 0000 950e 0000  p.r/0...........
    000000e0: 7800 0000 1001 ef00 2f00 0103 950e 0000  x......./.......
    000000f0: 736c 6565 7000 722f 3000 0000 0000 0000  sleep.r/0.......
    00000100: 950e 0000 7800 0000 0008 0000 0000 0000  ....x...........
    00000110: 7368 0064 0065 722f 3000 0000 0000 0000  sh.d.er/0.......
    00000120: b90d 0000 7800 0000 f0c7 e601 2f00 0103  ....x......./...
    00000130: b90d 0000 7368 0064 0065 722f 3000 0000  ....sh.d.er/0...
    00000140: 0000 0000 b90d 0000 7800 0000 0100 0000  ........x.......
    00000150: 0000 0000 736c 6565 7000 722f 3000 0000  ....sleep.r/0...
    00000160: 0000 0000 950e 0000 7800 0000 d030 0e00  ........x....0..
    00000170: 2f00 0103 950e 0000 736c 6565 7000 722f  /.......sleep.r/
    00000180: 3000 0000 0000 0000 950e 0000 7800 0000  0...........x...
    00000190: 4000 0000 0000 0000 6b77 6f72 6b65 722f  @.......kworker/
    000001a0: 7531 363a 3300 0000 610e 0000 7800 0000  u16:3...a...x...
    000001b0: 0000 0000 0000 0000 0000 0000 0000 0000  ................
    )",
};

}  // namespace

using perfetto::ExamplePage;
using perfetto::EventFilter;
using perfetto::ProtoTranslationTable;
using perfetto::ScatteredStreamNullDelegate;
using perfetto::GetTable;
using perfetto::PageFromXxd;
using perfetto::protos::pbzero::FtraceEventBundle;
using perfetto::CpuReader;

static void BM_ParsePageFullOfSchedSwitch(benchmark::State& state) {
  const ExamplePage* test_case = &g_six_sched_switch;

  ScatteredStreamNullDelegate delegate(kPageSize);
  protozero::ScatteredStreamWriter stream(&delegate);
  FtraceEventBundle writer;

  ProtoTranslationTable* table = GetTable(test_case->name);
  auto page = PageFromXxd(test_case->data);

  EventFilter filter(*table, std::set<std::string>({"sched_switch"}));

  while (state.KeepRunning()) {
    writer.Reset(&stream);
    CpuReader::ParsePage(42 /* cpu number */, page.get(), &filter, &writer,
                         table);
  }
}
BENCHMARK(BM_ParsePageFullOfSchedSwitch);
