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

#include "src/profiling/memory/heapprofd_producer.h"

#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>

#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/trace_writer.h"

namespace perfetto {
namespace profiling {
namespace {
constexpr char kHeapprofdDataSource[] = "android.heapprofd";
constexpr size_t kUnwinderQueueSize = 1000;
constexpr size_t kBookkeepingQueueSize = 1000;
constexpr size_t kUnwinderThreads = 5;
constexpr const char* kDumpOutput = "/data/local/tmp/heap_dump";
constexpr int kHeapprofdSignal = 36;

}  // namespace

HeapprofdProducer::HeapprofdProducer(base::TaskRunner* task_runner,
                                     TracingService::ProducerEndpoint* endpoint)
    : task_runner_(task_runner),
      endpoint_(endpoint),
      bookkeeping_queue_(kBookkeepingQueueSize),
      bookkeeping_thread_(kDumpOutput),
      unwinder_queues_(MakeUnwinderQueues(kUnwinderThreads)),
      unwinding_threads_(MakeUnwindingThreads(kUnwinderThreads)),
      socket_listener_(MakeSocketListenerCallback(), &bookkeeping_thread_),
      socket_(MakeSocket()),
      weak_factory_(this) {}

HeapprofdProducer::~HeapprofdProducer() = default;

void HeapprofdProducer::OnConnect() {
  DataSourceDescriptor desc;
  desc.set_name(kHeapprofdDataSource);
  endpoint_->RegisterDataSource(desc);
}

void HeapprofdProducer::OnDisconnect() {}
void HeapprofdProducer::SetupDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig& cfg) {
  if (cfg.name() != kHeapprofdDataSource)
    return;

  std::vector<pid_t> pids;
  for (uint64_t pid : cfg.heapprofd_config().pid())
    pids.emplace_back(static_cast<pid_t>(pid));

  if (pids.empty()) {
    // TODO(fmayer): Whole system profiling.
    PERFETTO_DLOG("No pids given");
    return;
  }

  auto it = data_sources_.find(id);
  if (it != data_sources_.end()) {
    PERFETTO_DFATAL("Received duplicated data source instance id: %" PRIu64,
                    id);
    return;
  }

  std::tie(it, std::ignore) = data_sources_.emplace(id, pids);
  DataSource& data_source = it->second;

  auto buffer_id = static_cast<BufferID>(cfg.target_buffer());
  data_source.trace_writer = endpoint_->CreateTraceWriter(buffer_id);

  ClientConfiguration client_config = MakeClientConfiguration(cfg);
  for (pid_t pid : pids) {
    data_source.sessions.emplace_back(
        socket_listener_.ExpectPID(pid, client_config));
  }
}

void HeapprofdProducer::StartDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig& cfg) {
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    PERFETTO_DFATAL("Received invalid data source instance to start: %" PRIu64,
                    id);
    return;
  }

  for (uint64_t pid : cfg.heapprofd_config().pid()) {
    if (kill(static_cast<pid_t>(pid), kHeapprofdSignal) != 0) {
      PERFETTO_DPLOG("kill");
    }
  }
}

void HeapprofdProducer::StopDataSource(DataSourceInstanceID id) {
  if (data_sources_.erase(id) != 1)
    PERFETTO_DFATAL("Trying to stop non existing data source: %" PRIu64, id);
}

void HeapprofdProducer::OnTracingSetup() {}
void HeapprofdProducer::Flush(FlushRequestID flush_id,
                              const DataSourceInstanceID* ids,
                              size_t num_ids) {
  size_t& flush_in_progress = flushes_in_progress_[flush_id];
  PERFETTO_DCHECK(flush_in_progress == 0);
  flush_in_progress = num_ids;
  for (size_t i = 0; i < num_ids; ++i) {
    auto it = data_sources_.find(ids[i]);
    if (it == data_sources_.end()) {
      PERFETTO_DFATAL("Trying to flush invalid data source.");
      continue;
    }
    const DataSource& data_source = it->second;
    DumpRecord dump_record;
    dump_record.pids = data_source.pids;
    dump_record.trace_writer = data_source.trace_writer;

    auto weak_producer = weak_factory_.GetWeakPtr();
    base::TaskRunner* task_runner = task_runner_;
    dump_record.callback = [task_runner, weak_producer, flush_id] {
      task_runner->PostTask([weak_producer, flush_id] {
        if (!weak_producer)
          return weak_producer->FinishDataSourceFlush(flush_id);
      });
    };
  }
}

void HeapprofdProducer::FinishDataSourceFlush(FlushRequestID flush_id) {
  size_t& flush_in_progress = flushes_in_progress_[flush_id];
  if (flush_in_progress == 0) {
    PERFETTO_DFATAL("Too many FinishDatasourceFlush for %" PRIu64, flush_id);
    return;
  }
  if (--flush_in_progress == 0)
    endpoint_->NotifyFlushComplete(flush_id);

  flushes_in_progress_.erase(flush_id);
}

std::function<void(UnwindingRecord)>
HeapprofdProducer::MakeSocketListenerCallback() {
  return [this](UnwindingRecord record) {
    unwinder_queues_[static_cast<size_t>(record.pid) % kUnwinderThreads].Add(
        std::move(record));
  };
}

std::vector<BoundedQueue<UnwindingRecord>>
HeapprofdProducer::MakeUnwinderQueues(size_t n) {
  std::vector<BoundedQueue<UnwindingRecord>> ret(n);
  for (size_t i = 0; i < n; ++i) {
    ret[i].SetCapacity(kUnwinderQueueSize);
  }
  return ret;
}

std::vector<std::thread> HeapprofdProducer::MakeUnwindingThreads(size_t n) {
  std::vector<std::thread> ret;
  for (size_t i = 0; i < n; ++i) {
    ret.emplace_back([this, i] {
      UnwindingMainLoop(&unwinder_queues_[i], &bookkeeping_queue_);
    });
  }
  return ret;
}

std::unique_ptr<base::UnixSocket> HeapprofdProducer::MakeSocket() {
  const char* sock_fd = getenv(kHeapprofdSocketEnvVar);
  if (sock_fd == nullptr) {
    unlink(kHeapprofdSocketFile);
    return base::UnixSocket::Listen(kHeapprofdSocketFile, &socket_listener_,
                                    task_runner_);
  }
  char* end;
  int raw_fd = static_cast<int>(strtol(sock_fd, &end, 10));
  if (*end != '\0')
    PERFETTO_FATAL("Invalid %s. Expected decimal integer.",
                   kHeapprofdSocketEnvVar);
  return base::UnixSocket::Listen(base::ScopedFile(raw_fd), &socket_listener_,
                                  task_runner_);
}

ClientConfiguration HeapprofdProducer::MakeClientConfiguration(
    const DataSourceConfig& cfg) {
  ClientConfiguration client_config;
  client_config.interval = cfg.heapprofd_config().sampling_interval_bytes();
  return client_config;
}

}  // namespace profiling
}  // namespace perfetto
