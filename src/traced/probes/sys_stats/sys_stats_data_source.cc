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

#include "src/traced/probes/sys_stats/sys_stats_data_source.h"

#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include "perfetto/base/file_utils.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/string_splitter.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/tracing/core/sys_stats_config.h"

#include "perfetto/common/sys_stats_counters.pbzero.h"
#include "perfetto/trace/sys_stats/sys_stats.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {
constexpr size_t kReadBufSize = 1024 * 16;
}  // namespace

// static
constexpr int SysStatsDataSource::kTypeId;

SysStatsDataSource::SysStatsDataSource(base::TaskRunner* task_runner,
                                       TracingSessionID session_id,
                                       std::unique_ptr<TraceWriter> writer,
                                       const DataSourceConfig& ds_config)
    : ProbesDataSource(session_id, kTypeId),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  const auto& config = ds_config.sys_stats_config();
  base::ignore_result(config);
  base::ignore_result(task_runner_);
  meminfo_fd_.reset(open("/proc/meminfo", O_RDONLY));
  if (!meminfo_fd_)
    PERFETTO_PLOG("Failed opening /proc/meminfo");
  read_buf_ = base::PageAllocator::Allocate(kReadBufSize);
}

SysStatsDataSource::~SysStatsDataSource() = default;

void SysStatsDataSource::ReadSysStats() {
  auto packet = writer_->NewTracePacket();
  auto* sys_stats = packet->set_sys_stats();
  ReadMeminfo(sys_stats);
}

void SysStatsDataSource::ReadMeminfo(protos::pbzero::SysStats* sys_stats) {
  if (!meminfo_fd_)
    return;
  ssize_t res = pread(*meminfo_fd_, read_buf_.get(), kReadBufSize - 1, 0);
  if (res <= 0) {
    PERFETTO_PLOG("Failed reading /proc/meminfo");
    meminfo_fd_.reset();
    return;
  }
  size_t rsize = static_cast<size_t>(res);
  char* buf = static_cast<char*>(read_buf_.get());
  buf[rsize] = '\0';
  for (base::StringSplitter lines(buf, rsize, '\n'); lines.Next();) {
    base::StringSplitter words(&lines, ' ');
    if (!words.Next())
      continue;
    auto it = meminfo_counters_.find(words.cur_token());
    if (it == meminfo_counters_.end())
      continue;
    int counter_id = it->second;
    if (!words.Next())
      continue;
    auto value = static_cast<int64_t>(strtoll(words.cur_token(), nullptr, 10));
    auto* meminfo = sys_stats->add_meminfo();
    meminfo->set_key(static_cast<protos::pbzero::MeminfoCounters>(counter_id));
    meminfo->set_value(value);
  }
}

base::WeakPtr<SysStatsDataSource> SysStatsDataSource::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

void SysStatsDataSource::Flush() {
  PERFETTO_CHECK(false);
}

}  // namespace perfetto
