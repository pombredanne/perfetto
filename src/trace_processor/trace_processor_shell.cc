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

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/time.h"
#include "perfetto/base/unix_task_runner.h"
#include "src/trace_processor/blob_reader.h"
#include "src/trace_processor/trace_processor.h"

#include "perfetto/trace_processor/raw_query.pb.h"

using namespace perfetto;
using namespace perfetto::trace_processor;

namespace {
class FileReader : public BlobReader {
 public:
  FileReader(const char* path) {
    fd_.reset(open(path, O_RDONLY));
    if (!fd_)
      PERFETTO_FATAL("Could not open %s", path);
    struct stat stat_buf {};
    PERFETTO_CHECK(fstat(*fd_, &stat_buf) == 0);
    file_size_ = static_cast<uint64_t>(stat_buf.st_size);
  }

  ~FileReader() override = default;

  uint32_t Read(uint64_t offset, uint32_t len, uint8_t* dst) override {
    ssize_t res = pread(*fd_, dst, len, static_cast<off_t>(offset));
    return res > 0 ? static_cast<uint32_t>(res) : 0;
  }

  size_t file_size() const { return file_size_; }

 private:
  base::ScopedFile fd_;
  uint64_t file_size_;
};

void PrintPrompt() {
  printf("\r%80s\r> ", "");
  fflush(stdout);
}

void OnQueryResult(protos::RawQueryResult res) {
  PERFETTO_CHECK(res.columns_size() == res.column_descriptors_size());
  if (res.has_error()) {
    PERFETTO_ELOG("SQLite error: %s", res.error().c_str());
    return;
  }

  for (int r = 0; r < static_cast<int>(res.num_records()); r++) {
    if (r % 32 == 0) {
      if (r > 0) {
        fprintf(stderr, "...\nType 'q' to stop, Enter for more records: ");
        fflush(stderr);
        char input[32];
        if (!fgets(input, sizeof(input) - 1, stdin))
          exit(0);
        if (input[0] == 'q')
          break;
      }
      for (const auto& col : res.column_descriptors())
        printf("%20s ", col.name().c_str());
      printf("\n");

      for (int i = 0; i < res.columns_size(); i++)
        printf("%20s ", "--------------------");
      printf("\n");
    }

    for (int c = 0; c < res.columns_size(); c++) {
      switch (res.column_descriptors(c).type()) {
        case protos::RawQueryResult_ColumnDesc_Type_STRING:
          printf("%20s ", res.columns(c).string_values(r).c_str());
          break;
        case protos::RawQueryResult_ColumnDesc_Type_DOUBLE:
          printf("%20f ", res.columns(c).double_values(r));
          break;
        case protos::RawQueryResult_ColumnDesc_Type_LONG:
          printf("%20lld ", res.columns(c).long_values(r));
          break;
      }
    }
    printf("\n");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PERFETTO_ELOG("Usage: %s trace_file.proto", argv[0]);
    return 1;
  }

  base::UnixTaskRunner task_runner;
  FileReader reader(argv[1]);
  TraceProcessor tp(&task_runner);

  task_runner.PostTask([&tp, &reader]() {
    auto t_start = base::GetWallTimeMs();
    auto on_trace_loaded = [t_start, &reader] {
      double s = (base::GetWallTimeMs() - t_start).count() / 1000.0;
      double size_mb = reader.file_size() / 1000000.0;
      PERFETTO_ILOG("Trace loaded: %.2f MB (%.1f MB/s)", size_mb, size_mb / s);
      PrintPrompt();
    };
    tp.LoadTrace(&reader, on_trace_loaded);
  });

  task_runner.AddFileDescriptorWatch(STDIN_FILENO, [&tp, &task_runner] {
    char line[1024];
    if (!fgets(line, sizeof(line) - 1, stdin))
      task_runner.Quit();
    protos::RawQueryArgs query;
    query.set_sql_query(line);
    tp.ExecuteQuery(query, OnQueryResult);
    PrintPrompt();
  });

  task_runner.Run();
  return 0;
}
