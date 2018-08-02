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
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/raw_query.pb.h"
#include "perfetto/trace_processor/sched.pb.h"
#include "src/base/test/test_task_runner.h"
#include "src/trace_processor/emscripten_task_runner.h"
#include "src/trace_processor/trace_database.h"

#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

const char* kAndroidSchedAndPsPath =
    "buildtools/example_traces/android_sched_and_ps.pb";

class FileReader : public BlobReader {
 public:
  FileReader(base::ScopedFile file) : file_(std::move(file)) {}

  ~FileReader() override = default;

  uint32_t Read(uint64_t offset, uint32_t len, uint8_t* dst) override {
    lseek(file_.get(), static_cast<off_t>(offset), SEEK_SET);
    return static_cast<uint32_t>(PERFETTO_EINTR(read(file_.get(), dst, len)));
  }

  base::ScopedFile file_;
};

TEST(TraceDatabaseIntegrationTest, CanLoadATrace) {
  FileReader reader(base::OpenFile(kAndroidSchedAndPsPath, O_RDONLY));
  base::TestTaskRunner task_runner;
  TraceDatabase db(&task_runner);

  auto loading_done = task_runner.CreateCheckpoint("loading_done");
  db.LoadTrace(&reader, [loading_done]() { loading_done(); });

  task_runner.RunUntilCheckpoint("loading_done");
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
