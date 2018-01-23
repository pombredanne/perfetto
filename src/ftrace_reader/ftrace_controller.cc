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

#include "perfetto/ftrace_reader/ftrace_controller.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <array>
#include <string>

#include "cpu_reader.h"
#include "event_info.h"
#include "ftrace_procfs.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"
#include "proto_translation_table.h"

#include "perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"

namespace perfetto {
namespace {

// TODO(b/68242551): Do not hardcode these paths.
const char kTracingPath[] = "/sys/kernel/debug/tracing/instances/meta/";

}  // namespace

// static
std::unique_ptr<FtraceController> FtraceController::Create(
    base::TaskRunner* runner) {
  auto ftrace_procfs =
      std::unique_ptr<FtraceProcfs>(new FtraceProcfs(kTracingPath));
  auto table = ProtoTranslationTable::Create(
      ftrace_procfs.get(), GetStaticEventInfo(), GetStaticCommonFieldsInfo());
  return std::unique_ptr<FtraceController>(
      new FtraceController(std::move(ftrace_procfs), runner, std::move(table)));
}

FtraceController::FtraceController(std::unique_ptr<FtraceProcfs> ftrace_procfs,
                                   base::TaskRunner* task_runner,
                                   std::unique_ptr<ProtoTranslationTable> table)
    : ftrace_procfs_(std::move(ftrace_procfs)),
      task_runner_(task_runner),
      enabled_count_(table->largest_id() + 1),
      table_(std::move(table)),
      weak_factory_(this) {}

FtraceController::~FtraceController() {
  for (size_t id = 1; id <= table_->largest_id(); id++) {
    if (enabled_count_[id]) {
      const Event* event = table_->GetEventById(id);
      ftrace_procfs_->DisableEvent(event->group, event->name);
    }
  }
  if (listening_for_raw_trace_data_) {
    sinks_.clear();
    StopIfNeeded();
  }
}

void FtraceController::StartIfNeeded() {
  if (sinks_.size() > 1)
    return;
  PERFETTO_CHECK(sinks_.size() != 0);
  PERFETTO_CHECK(!listening_for_raw_trace_data_);
  PERFETTO_CHECK(readers_.empty());
  listening_for_raw_trace_data_ = true;
  ftrace_procfs_->EnableTracing();
  generation_++;
  const size_t num_cpus = ftrace_procfs_->NumberOfCpus();
  readers_.reserve(num_cpus);
  for (size_t cpu = 0; cpu < num_cpus; cpu++) {
    // TODO should be pass a weak pointer to the reader? Think to destruction
    // order.
    readers_.emplace_back(std::unique_ptr<CpuReader>(
        new CpuReader(task_runner_, this, table_.get(), cpu,
                      ftrace_procfs_->OpenPipeForCpu(cpu))));
  }
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

void FtraceController::StopIfNeeded() {
  if (sinks_.size() != 0)
    return;
  PERFETTO_CHECK(listening_for_raw_trace_data_);
  listening_for_raw_trace_data_ = false;

  // This method will join() all the CpuReader threads.
  readers_.clear();

  ftrace_procfs_->DisableTracing();
}

void FtraceController::OnRawFtraceDataAvailable(size_t cpu) {
  PERFETTO_CHECK(cpu < readers_.size());
  CpuReader* reader = readers_[cpu].get();
  using BundleHandle =
      protozero::ProtoZeroMessageHandle<protos::pbzero::FtraceEventBundle>;
  // TODO: this |n| is a workaround to prevent that we fill the SMB
  // and end up in a state where we posted the notification to traced, but we
  // are stuck stalling in TraceWriterImpl and hence never send the IPC, hence
  // deadlock.
  for (int n = 0; n < 10; n++) {
    std::array<const EventFilter*, kMaxSinks> filters{};
    std::array<BundleHandle, kMaxSinks> bundles{};
    size_t sink_count = sinks_.size();
    size_t i = 0;
    for (FtraceSink* sink : sinks_) {
      filters[i] = sink->get_event_filter();
      bundles[i++] = sink->GetBundleForCpu(cpu);
    }
    bool has_data = reader->Drain(filters, bundles);
    i = 0;
    for (FtraceSink* sink : sinks_)
      sink->OnBundleComplete(cpu, std::move(bundles[i++]));
    PERFETTO_DCHECK(sinks_.size() == sink_count);
    if (!has_data)
      break;
  }
}

std::unique_ptr<FtraceSink> FtraceController::CreateSink(
    FtraceConfig config,
    FtraceSink::Delegate* delegate) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (sinks_.size() >= kMaxSinks)
    return nullptr;
  auto controller_weak = weak_factory_.GetWeakPtr();
  auto filter = std::unique_ptr<EventFilter>(
      new EventFilter(*table_.get(), config.events()));
  auto sink = std::unique_ptr<FtraceSink>(
      new FtraceSink(std::move(controller_weak), std::move(filter), delegate));
  Register(sink.get());
  return sink;
}

void FtraceController::Register(FtraceSink* sink) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto it_and_inserted = sinks_.insert(sink);
  PERFETTO_DCHECK(it_and_inserted.second);

  StartIfNeeded();
  for (const std::string& name : sink->enabled_events())
    RegisterForEvent(name);
}

void FtraceController::RegisterForEvent(const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  const Event* event = table_->GetEventByName(name);
  if (!event) {
    PERFETTO_DLOG("Can't enable %s, event not known", name.c_str());
    return;
  }
  size_t& count = enabled_count_.at(event->ftrace_event_id);
  if (count == 0)
    ftrace_procfs_->EnableEvent(event->group, event->name);
  count += 1;
}

void FtraceController::UnregisterForEvent(const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  const Event* event = table_->GetEventByName(name);
  if (!event)
    return;
  size_t& count = enabled_count_.at(event->ftrace_event_id);
  PERFETTO_CHECK(count > 0);
  if (--count == 0)
    ftrace_procfs_->DisableEvent(event->group, event->name);
}

void FtraceController::Unregister(FtraceSink* sink) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  size_t removed = sinks_.erase(sink);
  PERFETTO_DCHECK(removed == 1);

  for (const std::string& name : sink->enabled_events())
    UnregisterForEvent(name);
  StopIfNeeded();
}

FtraceSink::FtraceSink(base::WeakPtr<FtraceController> controller_weak,
                       std::unique_ptr<EventFilter> filter,
                       Delegate* delegate)
    : controller_weak_(std::move(controller_weak)),
      filter_(std::move(filter)),
      delegate_(delegate){};

FtraceSink::~FtraceSink() {
  if (controller_weak_)
    controller_weak_->Unregister(this);
};

const std::set<std::string>& FtraceSink::enabled_events() {
  return filter_->enabled_names();
}

FtraceConfig::FtraceConfig() = default;
FtraceConfig::FtraceConfig(std::set<std::string> events)
    : events_(std::move(events)) {}
FtraceConfig::~FtraceConfig() = default;

void FtraceConfig::AddEvent(const std::string& event) {
  events_.insert(event);
}

}  // namespace perfetto
