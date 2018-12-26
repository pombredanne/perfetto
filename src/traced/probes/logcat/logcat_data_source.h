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

#ifndef SRC_TRACED_PROBES_LOGCAT_LOGCAT_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_LOGCAT_LOGCAT_DATA_SOURCE_H_

#include <unordered_set>
#include <vector>

#include "perfetto/base/paged_memory.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/string_view.h"
#include "perfetto/base/unix_socket.h"
#include "perfetto/base/weak_ptr.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/traced/probes/probes_data_source.h"

namespace perfetto {

class DataSourceConfig;
class TraceWriter;
namespace base {
class TaskRunner;
}

class LogcatDataSource : public ProbesDataSource {
 public:
  struct Stats {
    uint64_t num_total = 0;           // Total number of log entries received.
    uint64_t num_parse_failures = 0;  // Parser failures.
    uint64_t num_skipped = 0;         // Messages skipped due to filters.
  };
  static constexpr int kTypeId = 6;

  LogcatDataSource(DataSourceConfig,
                   base::TaskRunner*,
                   TracingSessionID,
                   std::unique_ptr<TraceWriter> writer);

  ~LogcatDataSource() override;

  base::WeakPtr<LogcatDataSource> GetWeakPtr() const;

  // ProbesDataSource implementation.
  void Start() override;
  void Flush(FlushRequestID, std::function<void()> callback) override;
  const Stats& stats() const { return stats_; }

 private:
  struct DynamicLibLoader;

  void Tick(bool post_next_task);

  base::TaskRunner* const task_runner_;
  std::unique_ptr<TraceWriter> writer_;
  base::UnixSocketRaw logcat_sock_;
  uint32_t poll_rate_ms_ = 0;
  int min_prio_ = 0;
  std::unordered_set<base::StringView> filter_tags_;
  std::vector<char> filter_tags_strbuf_;
  std::string mode_;
  base::PagedMemory buf_;  // Safer than stack, has red zones around the buffer.
  Stats stats_;
  base::WeakPtrFactory<LogcatDataSource> weak_factory_;  // Keep last.
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_LOGCAT_LOGCAT_DATA_SOURCE_H_
