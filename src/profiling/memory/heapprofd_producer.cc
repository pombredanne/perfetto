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
constexpr const char* kDumpOutput = "/data/misc/peretto-traces/heap_dump";
constexpr int kHeapprofdSignal = 36;

void FindPidsForBinaries(std::vector<std::string> binaries,
                         std::vector<pid_t>* pids) {
  base::ScopedDir proc_dir(opendir("/proc"));
  if (!proc_dir) {
    PERFETTO_DFATAL("Failed to open /proc");
    return;
  }
  struct dirent* entry;
  while ((entry = readdir(*proc_dir))) {
    char* end;
    long int pid = strtol(entry->d_name, &end, 10);
    if (*end != '\0') {
      continue;
    }

    char link_buf[128];
    char binary_buf[128];

    if (snprintf(link_buf, sizeof(link_buf), "/proc/%lu/exe", pid) < 0) {
      PERFETTO_DFATAL("Failed to create exe filename for %lu", pid);
      continue;
    }
    ssize_t link_size = readlink(link_buf, binary_buf, sizeof(binary_buf));
    if (link_size < 0) {
      continue;
    }
    if (link_size == sizeof(binary_buf)) {
      PERFETTO_DFATAL("Potential overflow in binary name.");
      continue;
    }
    binary_buf[link_size] = '\0';
    for (const std::string& binary : binaries) {
      if (binary == binary_buf)
        pids->emplace_back(static_cast<pid_t>(pid));
    }
  }
}

}  // namespace

// We create kUnwinderThreads unwinding threads and one bookeeping thread.
// The bookkeeping thread is singleton in order to avoid expensive and
// complicated synchronisation in the bookkeeping.
//
// We wire up the system by creating BoundedQueues between the threads. The main
// thread runs the TaskRunner driving the SocketListener. The unwinding thread
// takes the data received by the SocketListener and if it is a malloc does
// stack unwinding, and if it is a free just forwards the content of the record
// to the bookkeeping thread.
//
//             +--------------+
//             |SocketListener|
//             +------+-------+
//                    |
//          +--UnwindingRecord -+
//          |                   |
// +--------v-------+   +-------v--------+
// |Unwinding Thread|   |Unwinding Thread|
// +--------+-------+   +-------+--------+
//          |                   |
//          +-BookkeepingRecord +
//                    |
//           +--------v---------+
//           |Bookkeeping Thread|
//           +------------------+

HeapprofdProducer::HeapprofdProducer(base::TaskRunner* task_runner,
                                     TracingService::ProducerEndpoint* endpoint)
    : task_runner_(task_runner),
      endpoint_(endpoint),
      bookkeeping_queue_(kBookkeepingQueueSize),
      bookkeeping_thread_(kDumpOutput),
      bookkeeping_th_([this] { bookkeeping_thread_.Run(&bookkeeping_queue_); }),
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
  PERFETTO_DLOG("Setting up data source.");
  if (cfg.name() != kHeapprofdDataSource) {
    PERFETTO_DLOG("Invalid data source name.");
    return;
  }

  std::vector<pid_t> pids;
  for (uint64_t pid : cfg.heapprofd_config().pid())
    pids.emplace_back(static_cast<pid_t>(pid));

  auto it = data_sources_.find(id);
  if (it != data_sources_.end()) {
    PERFETTO_DFATAL("Received duplicated data source instance id: %" PRIu64,
                    id);
    return;
  }

  FindPidsForBinaries(cfg.heapprofd_config().native_binary_name(), &pids);

  std::tie(it, std::ignore) = data_sources_.emplace(id, pids);
  DataSource& data_source = it->second;

  auto buffer_id = static_cast<BufferID>(cfg.target_buffer());
  data_source.trace_writer = endpoint_->CreateTraceWriter(buffer_id);

  ClientConfiguration client_config = MakeClientConfiguration(cfg);

  for (pid_t pid : pids) {
    data_source.sessions.emplace_back(
        socket_listener_.ExpectPID(pid, client_config));
  }

  if (pids.empty()) {
    // TODO(fmayer): Whole system profiling.
    PERFETTO_DLOG("No pids given");
  }
  PERFETTO_DLOG("Set up data source.");
}

void HeapprofdProducer::DoContiniousDump(DataSourceInstanceID id,
                                         uint32_t dump_interval) {
  if (Dump(id, 0, false)) {
    auto weak_producer = weak_factory_.GetWeakPtr();
    task_runner_->PostDelayedTask(
        [weak_producer, id, dump_interval] {
          if (!weak_producer)
            return;
          weak_producer->DoContiniousDump(id, dump_interval);
        },
        dump_interval);
  }
}

void HeapprofdProducer::StartDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig& cfg) {
  PERFETTO_DLOG("Start DataSource");
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    PERFETTO_DFATAL("Received invalid data source instance to start: %" PRIu64,
                    id);
    return;
  }

  const DataSource& data_source = it->second;

  for (pid_t pid : data_source.pids) {
    PERFETTO_DLOG("Sending %d to %d", kHeapprofdSignal, pid);
    if (kill(pid, kHeapprofdSignal) != 0) {
      PERFETTO_DPLOG("kill");
    }
  }

  const auto continuous_dump_config =
      cfg.heapprofd_config().continuous_dump_config();
  uint32_t dump_interval = continuous_dump_config.dump_interval_ms();
  if (dump_interval) {
    auto weak_producer = weak_factory_.GetWeakPtr();
    task_runner_->PostDelayedTask(
        [weak_producer, id, dump_interval] {
          if (!weak_producer)
            return;
          weak_producer->DoContiniousDump(id, dump_interval);
        },
        continuous_dump_config.dump_phase_ms());
  }
  PERFETTO_DLOG("Started DataSource");
}

void HeapprofdProducer::StopDataSource(DataSourceInstanceID id) {
  if (data_sources_.erase(id) != 1)
    PERFETTO_DFATAL("Trying to stop non existing data source: %" PRIu64, id);
}

void HeapprofdProducer::OnTracingSetup() {}

bool HeapprofdProducer::Dump(DataSourceInstanceID id,
                             FlushRequestID flush_id,
                             bool has_flush_id) {
  PERFETTO_DLOG("Dumping %" PRIu64 ", flush: %d", id, has_flush_id);
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    return false;
  }

  const DataSource& data_source = it->second;
  BookkeepingRecord record{};
  record.record_type = BookkeepingRecord::Type::Dump;
  DumpRecord& dump_record = record.dump_record;
  dump_record.pids = data_source.pids;
  dump_record.trace_writer = data_source.trace_writer;

  auto weak_producer = weak_factory_.GetWeakPtr();
  base::TaskRunner* task_runner = task_runner_;
  if (has_flush_id) {
    dump_record.callback = [task_runner, weak_producer, flush_id] {
      task_runner->PostTask([weak_producer, flush_id] {
        if (!weak_producer)
          return weak_producer->FinishDataSourceFlush(flush_id);
      });
    };
  } else {
    dump_record.callback = [] {};
  }

  bookkeeping_queue_.Add(std::move(record));
  return true;
}
void HeapprofdProducer::Flush(FlushRequestID flush_id,
                              const DataSourceInstanceID* ids,
                              size_t num_ids) {
  size_t& flush_in_progress = flushes_in_progress_[flush_id];
  PERFETTO_DCHECK(flush_in_progress == 0);
  flush_in_progress = num_ids;
  for (size_t i = 0; i < num_ids; ++i)
    Dump(ids[i], flush_id, true);
}

void HeapprofdProducer::FinishDataSourceFlush(FlushRequestID flush_id) {
  size_t& flush_in_progress = flushes_in_progress_[flush_id];
  if (flush_in_progress == 0) {
    PERFETTO_DFATAL("Too many FinishDataSourceFlush for %" PRIu64, flush_id);
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
