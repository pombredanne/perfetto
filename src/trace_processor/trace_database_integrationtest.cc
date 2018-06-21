/*
 * Copyright (C) 2017 The Android Open foo Project
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

#include "src/trace_processor/trace_database.h"

#include <chrono>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"
#include "src/base/test/test_task_runner.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;

class FileBlobReader : public BlobReader {
 public:
  FileBlobReader(const std::string& path)
      : file_(base::OpenFile(path, O_RDONLY)) {}
  ~FileBlobReader() override {}

  uint32_t Read(uint64_t offset, uint32_t len, uint8_t* dst) override {
    lseek(file_.get(), static_cast<off_t>(offset), SEEK_SET);
    return static_cast<uint32_t>(read(file_.get(), dst, len));
  }

 private:
  base::ScopedFile file_;
};

TEST(TraceDatabaseIntegrationTest, SimpleQuery) {
  base::TestTaskRunner runner;
  TraceDatabase database(&runner);

  FileBlobReader reader("/tmp/trace.protobuf");

  database.LoadTrace(&reader, runner.CreateCheckpoint("trace.load"));
  runner.RunUntilCheckpoint("trace.load");

  protos::RawQueryArgs args;
  args.set_sql_query("SELECT cpu, SUM(dur) from sched group by cpu");
  database.ExecuteQuery(args, [](protos::RawQueryResult result) {
    for (auto descriptor : result.column_descriptors()) {
      printf("%s\t", descriptor.name().c_str());
    }
    printf("\n");

    int row_count = static_cast<int>(result.num_records());
    for (int i = 0; i < row_count; i++) {
      for (int j = 0; j < result.columns_size(); j++) {
        switch (result.column_descriptors(j).type()) {
          case protos::RawQueryResult_ColumnDesc_Type_LONG:
            printf("%lld\t", result.columns(j).long_values(i));
            break;
          case protos::RawQueryResult_ColumnDesc_Type_STRING:
          case protos::RawQueryResult_ColumnDesc_Type_DOUBLE:
            break;
        }
      }
      printf("\n");
    }
  });
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
