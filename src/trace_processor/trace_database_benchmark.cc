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

#include <sys/stat.h>

#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"
#include "src/base/test/test_task_runner.h"
#include "src/trace_processor/trace_database.h"

namespace perfetto {
namespace trace_processor {
namespace {

class FileBlobReader : public BlobReader {
 public:
  FileBlobReader(const std::string& path)
      : file_(base::OpenFile(path, O_RDONLY)) {}
  ~FileBlobReader() override {}

  uint32_t Read(uint64_t offset, uint32_t len, uint8_t* dst) override {
    lseek(file_.get(), static_cast<off_t>(offset), SEEK_SET);
    return static_cast<uint32_t>(read(file_.get(), dst, len));
  }

  long GetFileSize() {
    struct stat stat_buf;
    int rc = fstat(file_.get(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
  }

 private:
  base::ScopedFile file_;
};

}  // namespace

static void BM_LoadTrace(benchmark::State& state) {
  base::TestTaskRunner runner;
  TraceDatabase database(&runner);
  FileBlobReader reader("/tmp/trace.protobuf");

  for (auto _ : state) {
    database.LoadTrace(&reader, runner.CreateCheckpoint("trace.load"));
    runner.RunUntilCheckpoint("trace.load");
  }
  state.SetBytesProcessed(static_cast<uint64_t>(state.iterations()) *
                          static_cast<uint64_t>(reader.GetFileSize()));
}
BENCHMARK(BM_LoadTrace);

}  // namespace trace_processor
}  // namespace perfetto
