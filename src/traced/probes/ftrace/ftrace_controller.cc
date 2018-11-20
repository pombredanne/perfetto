/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "src/traced/probes/ftrace/ftrace_controller.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <string>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/file_utils.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/metatrace.h"
#include "perfetto/base/time.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "src/traced/probes/ftrace/cpu_reader.h"
#include "src/traced/probes/ftrace/cpu_stats_parser.h"
#include "src/traced/probes/ftrace/event_info.h"
#include "src/traced/probes/ftrace/ftrace_config_muxer.h"
#include "src/traced/probes/ftrace/ftrace_data_source.h"
#include "src/traced/probes/ftrace/ftrace_metadata.h"
#include "src/traced/probes/ftrace/ftrace_procfs.h"
#include "src/traced/probes/ftrace/ftrace_stats.h"
#include "src/traced/probes/ftrace/proto_translation_table.h"


namespace perfetto {
namespace {

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
constexpr const char* kTracingPaths[] = {
    "/sys/kernel/tracing/", "/sys/kernel/debug/tracing/", nullptr,
};
#else
constexpr const char* kTracingPaths[] = {
    "/sys/kernel/debug/tracing/", nullptr,
};
#endif

constexpr int kDefaultDrainPeriodMs = 100;
constexpr int kFlushTimeoutMs = 250;
constexpr int kMinDrainPeriodMs = 1;
constexpr int kMaxDrainPeriodMs = 1000 * 60;

uint32_t ClampDrainPeriodMs(uint32_t drain_period_ms) {
  if (drain_period_ms == 0) {
    return kDefaultDrainPeriodMs;
  }
  if (drain_period_ms < kMinDrainPeriodMs ||
      kMaxDrainPeriodMs < drain_period_ms) {
    PERFETTO_LOG("drain_period_ms was %u should be between %u and %u",
                 drain_period_ms, kMinDrainPeriodMs, kMaxDrainPeriodMs);
    return kDefaultDrainPeriodMs;
  }
  return drain_period_ms;
}

void WriteToFile(const char* path, const char* str) {
  auto fd = base::OpenFile(path, O_WRONLY);
  if (!fd)
    return;
  base::ignore_result(base::WriteAll(*fd, str, strlen(str)));
}

void ClearFile(const char* path) {
  auto fd = base::OpenFile(path, O_WRONLY | O_TRUNC);
}

}  // namespace

// Method of last resort to reset ftrace state.
// We don't know what state the rest of the system and process is so as far
// as possible avoid allocations.
void HardResetFtraceState() {
  WriteToFile("/sys/kernel/debug/tracing/tracing_on", "0");
  WriteToFile("/sys/kernel/debug/tracing/buffer_size_kb", "4");
  WriteToFile("/sys/kernel/debug/tracing/events/enable", "0");
  ClearFile("/sys/kernel/debug/tracing/trace");

  WriteToFile("/sys/kernel/tracing/tracing_on", "0");
  WriteToFile("/sys/kernel/tracing/buffer_size_kb", "4");
  WriteToFile("/sys/kernel/tracing/events/enable", "0");
  ClearFile("/sys/kernel/tracing/trace");
}

// static
// TODO(taylori): Add a test for tracing paths in integration tests.
std::unique_ptr<FtraceController> FtraceController::Create(
    base::TaskRunner* runner,
    Observer* observer) {
  size_t index = 0;
  std::unique_ptr<FtraceProcfs> ftrace_procfs = nullptr;
  while (!ftrace_procfs && kTracingPaths[index]) {
    ftrace_procfs = FtraceProcfs::Create(kTracingPaths[index++]);
  }

  if (!ftrace_procfs)
    return nullptr;

  auto table = ProtoTranslationTable::Create(
      ftrace_procfs.get(), GetStaticEventInfo(), GetStaticCommonFieldsInfo());

  std::unique_ptr<FtraceConfigMuxer> model = std::unique_ptr<FtraceConfigMuxer>(
      new FtraceConfigMuxer(ftrace_procfs.get(), table.get()));
  return std::unique_ptr<FtraceController>(
      new FtraceController(std::move(ftrace_procfs), std::move(table),
                           std::move(model), runner, observer));
}

FtraceController::FtraceController(std::unique_ptr<FtraceProcfs> ftrace_procfs,
                                   std::unique_ptr<ProtoTranslationTable> table,
                                   std::unique_ptr<FtraceConfigMuxer> model,
                                   base::TaskRunner* task_runner,
                                   Observer* observer)
    : task_runner_(task_runner),
      observer_(observer),
      ftrace_procfs_(std::move(ftrace_procfs)),
      table_(std::move(table)),
      ftrace_config_muxer_(std::move(model)),
      weak_factory_(this) {}

FtraceController::~FtraceController() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (const auto* data_source : data_sources_)
    ftrace_config_muxer_->RemoveConfig(data_source->config_id());
  data_sources_.clear();
  started_data_sources_.clear();
  StopIfNeeded();
}

uint64_t FtraceController::NowMs() const {
  return static_cast<uint64_t>(base::GetWallTimeMs().count());
}

void FtraceController::DrainCPUs(size_t generation) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_METATRACE("DrainCPUs()", 0);

  // We might have stopped tracing then quickly re-enabled it, in this case
  // we don't want to end up with two periodic tasks for each CPU:
  if (generation_ != generation)
    return;

  const size_t num_cpus = ftrace_procfs_->NumberOfCpus();
  FlushRequestID ack_flush_request_id = 0;
  std::bitset<base::kMaxCpus> cpus_to_drain;
  {
    std::unique_lock<std::mutex> lock(thread_sync_.mutex);
    std::swap(cpus_to_drain, thread_sync_.cpus_to_drain);

    if (cur_flush_request_id_) {
      PERFETTO_DLOG("DrainCpus flush ack: %zu", thread_sync_.flush_acks.count());
    }
    // Check also if a flush is pending and if all cpus have acked. If that's
    // the case, ack the overall Flush() request at the end of this function.
    if (cur_flush_request_id_ && thread_sync_.flush_acks.count() >= num_cpus) {
      thread_sync_.flush_acks.reset();
      ack_flush_request_id = cur_flush_request_id_;
      cur_flush_request_id_ = 0;
    }
  }

  for (size_t cpu = 0; cpu < num_cpus; cpu++) {
    if (!cpus_to_drain[cpu])
      continue;
    // This method reads the pipe and converts the raw ftrace data into
    // protobufs using the |data_source|'s TraceWriter.
    cpu_readers_[cpu]->Drain(started_data_sources_);
    OnDrainCpuForTesting(cpu);
  }

  // If we filled up any SHM pages while draining the data, we will have posted
  // a task to notify traced about this. Only unblock the readers after this
  // notification is sent to make it less likely that they steal CPU time away
  // from traced.
  // If this drain was due to a flush request, UnblockReaders will be a no-op.
  base::WeakPtr<FtraceController> weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this] {
    if (weak_this)
      weak_this->UnblockReaders();
  });

  observer_->OnFtraceDataWrittenIntoDataSourceBuffers();

  if (ack_flush_request_id) {
    {
      std::unique_lock<std::mutex> lock(thread_sync_.mutex);
      if (thread_sync_.cmd == FtraceThreadSync::kFlush)
        IssueThreadSyncCmd(FtraceThreadSync::kRun, std::move(lock));
    }
    // This will call FtraceDataSource::OnFtraceFlushComplete(), which in turn
    // will flush the userspace buffers and ack the flush to the ProbesProducer.
    NotifyFlushCompleteToStartedDataSources(ack_flush_request_id);
  }
}

void FtraceController::UnblockReaders() {
  PERFETTO_METATRACE("UnblockReaders()", 0);

  // If a flush or a quit is pending, do nothing.
  std::unique_lock<std::mutex> lock(thread_sync_.mutex);
  if (thread_sync_.cmd != FtraceThreadSync::kRun)
    return;

  // Unblock all waiting readers to start moving more data into their
  // respective staging pipes.
  IssueThreadSyncCmd(FtraceThreadSync::kRun, std::move(lock));
}

void FtraceController::StartIfNeeded() {
  if (started_data_sources_.size() > 1)
    return;
  PERFETTO_DCHECK(!started_data_sources_.empty());
  PERFETTO_DCHECK(cpu_readers_.empty());
  generation_++;
  base::WeakPtr<FtraceController> weak_this = weak_factory_.GetWeakPtr();

  {
    std::unique_lock<std::mutex> lock(thread_sync_.mutex);
    thread_sync_.cmd = FtraceThreadSync::kRun;
    thread_sync_.cmd_id++;
  }

  for (size_t cpu = 0; cpu < ftrace_procfs_->NumberOfCpus(); cpu++) {
    cpu_readers_.emplace(
        cpu, std::unique_ptr<CpuReader>(new CpuReader(
                 table_.get(), &thread_sync_, cpu,
                 ftrace_procfs_->OpenPipeForCpu(cpu),
                 std::bind(&FtraceController::OnDataAvailable, this, weak_this,
                           generation_, cpu, GetDrainPeriodMs()))));
  }
}

uint32_t FtraceController::GetDrainPeriodMs() {
  if (data_sources_.empty())
    return kDefaultDrainPeriodMs;
  uint32_t min_drain_period_ms = kMaxDrainPeriodMs + 1;
  for (const FtraceDataSource* data_source : data_sources_) {
    if (data_source->config().drain_period_ms() < min_drain_period_ms)
      min_drain_period_ms = data_source->config().drain_period_ms();
  }
  return ClampDrainPeriodMs(min_drain_period_ms);
}

void FtraceController::ClearTrace() {
  ftrace_procfs_->ClearTrace();
}

void FtraceController::DisableAllEvents() {
  ftrace_procfs_->DisableAllEvents();
}

void FtraceController::WriteTraceMarker(const std::string& s) {
  ftrace_procfs_->WriteTraceMarker(s);
}

void FtraceController::Flush(FlushRequestID flush_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  if (flush_id == cur_flush_request_id_)
    return;  // Already dealing with this flush request.

  cur_flush_request_id_ = flush_id;
  {
    std::unique_lock<std::mutex> lock(thread_sync_.mutex);
    thread_sync_.flush_acks.reset();
    IssueThreadSyncCmd(FtraceThreadSync::kFlush, std::move(lock));
  }

  // TODO who resets the state to kRun?

  base::WeakPtr<FtraceController> weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, flush_id] {
        if (weak_this)
          weak_this->OnFlushTimeout(flush_id);
      },
      kFlushTimeoutMs);
}

void FtraceController::OnFlushTimeout(FlushRequestID flush_request_id) {
  if (flush_request_id != cur_flush_request_id_)
    return;
  unsigned long acks = 0;

  {
    // Unlock the cpu readers and move on.
    std::unique_lock<std::mutex> lock(thread_sync_.mutex);
    acks = thread_sync_.flush_acks.to_ulong();
    thread_sync_.flush_acks.reset();
    if (thread_sync_.cmd == FtraceThreadSync::kFlush)
      IssueThreadSyncCmd(FtraceThreadSync::kRun, std::move(lock));
  }

  PERFETTO_ELOG("Flush(%" PRIu64 ") timed out. Acked cpu set: 0x%lx",
                flush_request_id, acks);
  cur_flush_request_id_ = 0;
  NotifyFlushCompleteToStartedDataSources(flush_request_id);
}

void FtraceController::StopIfNeeded() {
  if (!started_data_sources_.empty())
    return;

  // We are not implicitly flushing on Stop. The tracing service is supposed to
  // ask for an explicit flush before stopping, unless it needs to perform a
  // non-graceful stop.

  IssueThreadSyncCmd(FtraceThreadSync::kQuit);
  cpu_readers_.clear();
  generation_++;
}

// This method is called on the worker thread. Lifetime is guaranteed to be
// valid, because the FtraceController dtor (that happens on the main thread)
// joins the worker threads. |weak_this| is passed and not derived, because the
// WeakPtrFactory is accessible only on the main thread.
void FtraceController::OnDataAvailable(
    base::WeakPtr<FtraceController> weak_this,
    size_t generation,
    size_t cpu,
    uint32_t drain_period_ms) {
  PERFETTO_DCHECK(cpu < ftrace_procfs_->NumberOfCpus());
  PERFETTO_METATRACE("OnDataAvailable()", cpu);

  // TODO we should probabl have a start/stop state at the controllerlevel and
  // quit here, instead of relying on kQuit being final.
  bool post_drain_task = false;
  {
    std::unique_lock<std::mutex> lock(thread_sync_.mutex);
    switch (thread_sync_.cmd) {
      case FtraceThreadSync::kQuit:
        return;  // Data arrived too late, ignore.

      case FtraceThreadSync::kRun:
        break;

      case FtraceThreadSync::kFlush:
        // In the case of a flush, drain soon to reduce flush latency. Flush
        // should be quite rare and we don't care about extra cpu/wakeups
        // required for the aggressive drain.
        drain_period_ms = 0;
        break;
    }
    // If this was the first CPU to wake up, schedule a drain for the next
    // drain interval.
    post_drain_task = thread_sync_.cpus_to_drain.none();
    thread_sync_.cpus_to_drain[cpu] = true;
  }  // lock(thread_sync_.mutex)

  if (!post_drain_task)
    return;

  task_runner_->PostDelayedTask(
      [weak_this, generation] {
        if (weak_this)
          weak_this->DrainCPUs(generation);
      },
      drain_period_ms ? (drain_period_ms - (NowMs() % drain_period_ms)) : 0);
}

bool FtraceController::AddDataSource(FtraceDataSource* data_source) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!ValidConfig(data_source->config()))
    return false;

  auto config_id = ftrace_config_muxer_->SetupConfig(data_source->config());
  if (!config_id)
    return false;

  std::unique_ptr<EventFilter> filter(new EventFilter(
      *table_, FtraceEventsAsSet(*ftrace_config_muxer_->GetConfig(config_id))));
  auto it_and_inserted = data_sources_.insert(data_source);
  PERFETTO_DCHECK(it_and_inserted.second);
  data_source->Initialize(config_id, std::move(filter));
  return true;
}

bool FtraceController::StartDataSource(FtraceDataSource* data_source) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  FtraceConfigId config_id = data_source->config_id();
  PERFETTO_CHECK(config_id);

  if (!ftrace_config_muxer_->ActivateConfig(config_id))
    return false;

  started_data_sources_.insert(data_source);
  StartIfNeeded();
  return true;
}

void FtraceController::RemoveDataSource(FtraceDataSource* data_source) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  started_data_sources_.erase(data_source);
  size_t removed = data_sources_.erase(data_source);
  if (!removed)
    return;  // Can happen if AddDataSource failed (e.g. too many sessions).
  ftrace_config_muxer_->RemoveConfig(data_source->config_id());
  StopIfNeeded();
}

void FtraceController::DumpFtraceStats(FtraceStats* stats) {
  DumpAllCpuStats(ftrace_procfs_.get(), stats);
}

void FtraceController::IssueThreadSyncCmd(
    FtraceThreadSync::Cmd cmd,
    std::unique_lock<std::mutex> already_locked) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  {
    std::unique_lock<std::mutex> lock(std::move(already_locked));
    if (!lock.owns_lock())
      lock = std::unique_lock<std::mutex>(thread_sync_.mutex);

    // If in kQuit state, we should never issue any other commands.
    PERFETTO_DCHECK(thread_sync_.cmd != FtraceThreadSync::kQuit ||
                    cmd == FtraceThreadSync::kQuit);
    thread_sync_.cmd = cmd;
    thread_sync_.cmd_id++;
  }

  // Send a SIGPIPE to all worker threads to wake them up if they are sitting in
  // a blocking splice(). If they are not and instead they are sitting in the
  // cond-variable.wait(), this, together with the one below, will have at best
  // the same effect of a spurious wakeup, depending on the implementation of
  // the condition variable.
  for (const auto& kv : cpu_readers_)
    kv.second->InterruptWorkerThreadWithSignal();

  thread_sync_.cond.notify_all();
}

void FtraceController::NotifyFlushCompleteToStartedDataSources(
    FlushRequestID flush_request_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (FtraceDataSource* data_source : started_data_sources_)
    data_source->OnFtraceFlushComplete(flush_request_id);
}

FtraceController::Observer::~Observer() = default;

}  // namespace perfetto
