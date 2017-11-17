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
#include "base/scoped_file.h"
#include "base/utils.h"
#include "ftrace_to_proto_translation_table.h"

namespace perfetto {

namespace {

// TODO(b/68242551): Do not hardcode these paths.
const char kTracingPath[] = "/sys/kernel/debug/tracing/";

// Reading this file produces human readable trace output.
// Writing to this file clears all trace buffers for all CPUS.
const char kTracePath[] = "/sys/kernel/debug/tracing/trace";

// Writing to this file injects an event into the trace buffer.
const char kTraceMarkerPath[] = "/sys/kernel/debug/tracing/trace_marker";

// Reading this file returns 1/0 if tracing is enabled/disabled.
// Writing 1/0 to this file enables/disables tracing.
// Disabling tracing with this file prevents further writes but
// does not clear the buffer.
const char kTracingOnPath[] = "/sys/kernel/debug/tracing/tracing_on";

char ReadOneCharFromFile(const std::string& path) {
  base::ScopedFile fd(open(path.c_str(), O_RDONLY));
  if (!fd)
    return '\0';
  char result = '\0';
  ssize_t bytes = PERFETTO_EINTR(read(fd.get(), &result, 1));
  PERFETTO_DCHECK(bytes == 1 || bytes == -1);
  return result;
}

std::string TracePipeRawPath(const std::string& root, size_t cpu) {
  return root + "per_cpu/" + std::to_string(cpu) + "/trace_pipe_raw";
}

}  // namespace

// static
std::unique_ptr<FtraceController> FtraceController::Create(
    base::TaskRunner* runner) {
  auto impl = std::unique_ptr<SystemImpl>(new SystemImpl);
  auto table = FtraceToProtoTranslationTable::Create(kTracingPath);
  return std::unique_ptr<FtraceController>(
      new FtraceController(std::move(impl), kTracingPath, runner, std::move(table)));
}

FtraceController::FtraceController(
    std::unique_ptr<SystemImpl> impl,
    const std::string& root,
    base::TaskRunner* task_runner,
    std::unique_ptr<FtraceToProtoTranslationTable> table)
    : impl_(std::move(impl)),
      root_(root),
      task_runner_(task_runner),
      weak_factory_(this),
      enabled_count_(table->largest_id() + 1),
      table_(std::move(table)) {}
FtraceController::~FtraceController() {
  for (size_t id=1; id<=table_->largest_id(); id++) {
    if (enabled_count_[id]) {
      const FtraceToProtoTranslationTable::Event* event = table_->GetEventById(id);
      DisableEvent(event->group, event->name);
    }
  }
};

void FtraceController::Start() {
  if (running_)
    return;
  running_ = true;
  for (size_t cpu = 0; cpu < impl_->NumberOfCpus(); cpu++) {
    FtraceCpuReader* reader = GetCpuReader(cpu);
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
  for (size_t cpu = 0; cpu < impl_->NumberOfCpus(); cpu++) {
    FtraceCpuReader* reader = GetCpuReader(cpu);
    int fd = reader->GetFileDescriptor();
    task_runner_->RemoveFileDescriptorWatch(fd);
  }
}

void FtraceController::CpuReady(size_t cpu) {
  FtraceCpuReader* reader = GetCpuReader(cpu);
  reader->Read();
}

void FtraceController::ClearTrace() {
  base::ScopedFile fd(open(kTracePath, O_WRONLY | O_TRUNC));
  PERFETTO_CHECK(fd);  // Could not clear.
}

bool FtraceController::WriteTraceMarker(const std::string& str) {
  return impl_->WriteToFile(kTraceMarkerPath, str);
}

bool FtraceController::EnableTracing() {
  return impl_->WriteToFile(kTracingOnPath, "1");
}

bool FtraceController::DisableTracing() {
  return impl_->WriteToFile(kTracingOnPath, "0");
}

bool FtraceController::IsTracingEnabled() {
  return ReadOneCharFromFile(kTracingOnPath) == '1';
}

bool FtraceController::EnableEvent(const std::string& group,
                                   const std::string& name) {
  // The events directory contains the 'format' and 'enable' files for each event.
  // These are nested like so: group_name/event_name/{format, enable}
  std::string path = root_ + "events/" + group + "/" + name + "/enable";
  return impl_->WriteToFile(path, "1");
}

bool FtraceController::DisableEvent(const std::string& group,
                                    const std::string& name) {
  std::string path = root_ + "events/" + group + "/" + name + "/enable";
  return impl_->WriteToFile(path, "0");
}

FtraceCpuReader* FtraceController::GetCpuReader(size_t cpu) {
  if (cpu >= impl_->NumberOfCpus())
    return nullptr;
  if (!readers_.count(cpu)) {
    std::string path = TracePipeRawPath(root_, cpu);
    readers_.emplace(cpu, CreateCpuReader(table_.get(), cpu, path));
  }
  return &readers_.at(cpu);
}

FtraceCpuReader FtraceController::CreateCpuReader(const FtraceToProtoTranslationTable* table, size_t cpu, const std::string& path) {
  auto fd = base::ScopedFile(open(path.c_str(), O_RDONLY));
  return FtraceCpuReader(table, cpu, std::move(fd));
}

size_t FtraceController::SystemImpl::NumberOfCpus() const {
  static size_t num_cpus = sysconf(_SC_NPROCESSORS_CONF);
  return num_cpus;
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
  const FtraceToProtoTranslationTable::Event* event =
      table_->GetEventByName(name);
  if (!event) {
    PERFETTO_DLOG("Can't enable %s, event not known", name.c_str());
    return;
  }
  size_t& count = enabled_count_.at(event->ftrace_event_id);
  if (count == 0)
    EnableEvent(event->group, event->name);
  count += 1;
}

void FtraceController::UnregisterForEvent(const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  const FtraceToProtoTranslationTable::Event* event =
      table_->GetEventByName(name);
  if (!event)
    return;
  size_t& count = enabled_count_.at(event->ftrace_event_id);
  PERFETTO_CHECK(count > 0);
  count -= 1;
  if (count == 0)
    DisableEvent(event->group, event->name);
}

void FtraceController::Unregister(FtraceSink* sink) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  size_t removed = sinks_.erase(sink);
  PERFETTO_DCHECK(removed == 1);
  for (const std::string& name : sink->enabled_events())
    UnregisterForEvent(name);
}

bool FtraceController::SystemImpl::WriteToFile(const std::string& path, const std::string& str) {
  base::ScopedFile fd(open(path.c_str(), O_WRONLY));
  if (!fd)
    return false;
  ssize_t written = PERFETTO_EINTR(write(fd.get(), str.c_str(), str.length()));
  ssize_t length = static_cast<ssize_t>(str.length());
  // This should either fail or write fully.
  PERFETTO_DCHECK(written == length || written == -1);
  return written == length;
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
