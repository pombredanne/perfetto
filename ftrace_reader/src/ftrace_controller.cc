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

#include "ftrace_reader/ftrace_controller.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include "base/logging.h"
#include "base/utils.h"
#include "cpu_reader.h"
#include "ftrace_api.h"
#include "proto_translation_table.h"

namespace perfetto {

namespace {

// TODO(b/68242551): Do not hardcode these paths.
const char kTracingPath[] = "/sys/kernel/debug/tracing/";

}  // namespace

// static
std::unique_ptr<FtraceController> FtraceController::Create(
    base::TaskRunner* runner) {
  auto ftrace_api = std::unique_ptr<FtraceApi>(new FtraceApi(kTracingPath));
  auto table = ProtoTranslationTable::Create(kTracingPath, ftrace_api.get());
  return std::unique_ptr<FtraceController>(
      new FtraceController(std::move(ftrace_api), runner, std::move(table)));
}

FtraceController::FtraceController(std::unique_ptr<FtraceApi> ftrace_api,
                                   base::TaskRunner* task_runner,
                                   std::unique_ptr<ProtoTranslationTable> table)
    : ftrace_api_(std::move(ftrace_api)),
      task_runner_(task_runner),
      weak_factory_(this),
      enabled_count_(table->largest_id() + 1),
      table_(std::move(table)) {}
FtraceController::~FtraceController() {
  for (size_t id = 1; id <= table_->largest_id(); id++) {
    if (enabled_count_[id]) {
      const ProtoTranslationTable::Event* event = table_->GetEventById(id);
      ftrace_api_->DisableEvent(event->group, event->name);
    }
  }
};

void FtraceController::Start() {
  if (running_)
    return;
  running_ = true;
  for (size_t cpu = 0; cpu < ftrace_api_->NumberOfCpus(); cpu++) {
    CpuReader* reader = GetCpuReader(cpu);
    int fd = reader->GetFileDescriptor();
    base::WeakPtr<FtraceController> ptr = weak_factory_.GetWeakPtr();
    task_runner_->AddFileDescriptorWatch(fd, [ptr, cpu]() {
      // The controller has gone already.
      if (!ptr)
        return;
      ptr.get()->CpuReady(cpu);
    });
  }
}

void FtraceController::Stop() {
  if (!running_)
    return;
  running_ = false;
  for (size_t cpu = 0; cpu < ftrace_api_->NumberOfCpus(); cpu++) {
    CpuReader* reader = GetCpuReader(cpu);
    int fd = reader->GetFileDescriptor();
    task_runner_->RemoveFileDescriptorWatch(fd);
  }
}

void FtraceController::CpuReady(size_t cpu) {
  CpuReader* reader = GetCpuReader(cpu);
  reader->Read();
}

CpuReader* FtraceController::GetCpuReader(size_t cpu) {
  if (cpu >= ftrace_api_->NumberOfCpus())
    return nullptr;
  if (!readers_.count(cpu)) {
    std::string path = ftrace_api_->GetTracePipeRawPath(cpu);
    readers_.emplace(cpu, CreateCpuReader(table_.get(), cpu, path));
  }
  return readers_.at(cpu).get();
}

std::unique_ptr<CpuReader> FtraceController::CreateCpuReader(
    const ProtoTranslationTable* table,
    size_t cpu,
    const std::string& path) {
  return std::unique_ptr<CpuReader>(
      new CpuReader(table, cpu, ftrace_api_->OpenFile(path)));
}

std::unique_ptr<FtraceSink> FtraceController::CreateSink(
    FtraceConfig config,
    FtraceSink::Delegate* delegate) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto controller_weak = weak_factory_.GetWeakPtr();
  auto sink = std::unique_ptr<FtraceSink>(
      new FtraceSink(std::move(controller_weak), std::move(config)));
  Register(sink.get());
  return sink;
}

void FtraceController::Register(FtraceSink* sink) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto it_and_inserted = sinks_.insert(sink);
  PERFETTO_DCHECK(it_and_inserted.second);
  for (const std::string& name : sink->enabled_events())
    RegisterForEvent(name);
}

void FtraceController::RegisterForEvent(const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  const ProtoTranslationTable::Event* event = table_->GetEventByName(name);
  if (!event) {
    PERFETTO_DLOG("Can't enable %s, event not known", name.c_str());
    return;
  }
  size_t& count = enabled_count_.at(event->ftrace_event_id);
  if (count == 0)
    ftrace_api_->EnableEvent(event->group, event->name);
  count += 1;
}

void FtraceController::UnregisterForEvent(const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  const ProtoTranslationTable::Event* event = table_->GetEventByName(name);
  if (!event)
    return;
  size_t& count = enabled_count_.at(event->ftrace_event_id);
  PERFETTO_CHECK(count > 0);
  count -= 1;
  if (count == 0)
    ftrace_api_->DisableEvent(event->group, event->name);
}

void FtraceController::Unregister(FtraceSink* sink) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  size_t removed = sinks_.erase(sink);
  PERFETTO_DCHECK(removed == 1);
  for (const std::string& name : sink->enabled_events())
    UnregisterForEvent(name);
}

FtraceSink::FtraceSink(base::WeakPtr<FtraceController> controller_weak,
                       FtraceConfig config)
    : controller_weak_(std::move(controller_weak)),
      config_(std::move(config)){};

FtraceSink::~FtraceSink() {
  if (controller_weak_)
    controller_weak_->Unregister(this);
};

FtraceConfig::FtraceConfig() = default;
FtraceConfig::FtraceConfig(std::set<std::string> events) : events_(events) {}
FtraceConfig::~FtraceConfig() = default;

void FtraceConfig::AddEvent(const std::string& event) {
  events_.insert(event);
}

}  // namespace perfetto
