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

#include "ftrace_model.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>

#include "perfetto/base/utils.h"
#include "proto_translation_table.h"

namespace perfetto {
namespace {

// trace_clocks in preference order.
const char* kClocks[] = {
    "boot", "global", "local", nullptr,
};

// const int kDefaultDrainPeriodMs = 100;
// const int kMinDrainPeriodMs = 1;
// const int kMaxDrainPeriodMs = 1000 * 60;

const int kDefaultPerCpuBufferSizeKb = 512;   // 512kb
const int kMaxPerCpuBufferSizeKb = 2 * 1024;  // 2mb

std::vector<std::string> difference(const std::set<std::string>& a,
                                    const std::set<std::string>& b) {
  std::vector<std::string> result(std::max(b.size(), a.size()));
  {
    auto it = std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                                  result.begin());
    result.resize(it - result.begin());
  }
  return result;
}

bool RunAtrace(std::vector<std::string> args) {
  int status = 1;

  std::vector<char*> argv;
  // args, and then a null.
  argv.reserve(1 + args.size());
  for (const auto& arg : args)
    argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);

  pid_t pid = fork();
  PERFETTO_CHECK(pid >= 0);
  if (pid == 0) {
    execv("/system/bin/atrace", &argv[0]);
    // Reached only if execv fails.
    _exit(1);
  }
  waitpid(pid, &status, 0);
  return status == 0;
}

}  // namespace

std::set<std::string> GetFtraceEvents(const FtraceConfig& request) {
  std::set<std::string> events;
  events.insert(request.event_names().begin(), request.event_names().end());
  if (RequiresAtrace(request)) {
    events.insert("print");
  }
  return events;
}

// Post-conditions:
// 1. result >= 1 (should have at least one page per CPU)
// 2. result * 4 < kMaxTotalBufferSizeKb
// 3. If input is 0 output is a good default number.
size_t ComputeCpuBufferSizeInPages(size_t requested_buffer_size_kb) {
  if (requested_buffer_size_kb == 0)
    requested_buffer_size_kb = kDefaultPerCpuBufferSizeKb;
  if (requested_buffer_size_kb > kMaxPerCpuBufferSizeKb)
    requested_buffer_size_kb = kDefaultPerCpuBufferSizeKb;

  size_t pages = requested_buffer_size_kb / (base::kPageSize / 1024);
  if (pages == 0)
    return 1;

  return pages;
}

FtraceModel::FtraceModel(FtraceProcfs* ftrace,
                         const ProtoTranslationTable* table)
    : ftrace_(ftrace), table_(table), current_state_(), configs_(){};
FtraceModel::~FtraceModel() = default;

FtraceConfigId FtraceModel::RequestConfig(const FtraceConfig& request) {
  FtraceConfig actual;

  bool is_ftrace_enabled = ftrace_->IsTracingEnabled();
  if (configs_.empty()) {
    PERFETTO_DCHECK(!current_state_.tracing_on());

    // If someone else is using ftrace give up now.
    if (is_ftrace_enabled)
      return kInvalidFtraceConfig;

// If we're about to turn tracing on use this opportunity do some setup:
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    EnableAtrace(request);
#endif
    SetupClock(request);
    SetupBufferSize(request);
  } else {
    // Did someone turn ftrace off behind our back? If so give up.
    if (!is_ftrace_enabled)
      return kInvalidFtraceConfig;
  }

  std::set<std::string> events = GetFtraceEvents(request);

  for (auto& name : events) {
    const Event* event = table_->GetEventByName(name);
    if (!event) {
      PERFETTO_DLOG("Can't enable %s, event not known", name.c_str());
      continue;
    }
    if (current_state_.ftrace_events().count(name) ||
        std::string("ftrace") == event->group) {
      *actual.add_event_names() = name;
      continue;
    }
    if (ftrace_->EnableEvent(event->group, event->name)) {
      current_state_.mutable_ftrace_events()->insert(name);
      *actual.add_event_names() = name;
    }
  }

  if (configs_.empty()) {
    PERFETTO_DCHECK(!current_state_.tracing_on());
    ftrace_->EnableTracing();
    current_state_.set_tracing_on(true);
  }

  FtraceConfigId id = GetNextId();
  configs_.emplace(id, std::move(actual));
  return id;
}

bool FtraceModel::RemoveConfig(FtraceConfigId id) {
  if (!configs_.count(id))
    return false;

  size_t removed = configs_.erase(id);
  PERFETTO_DCHECK(removed == 1);

  std::set<std::string> expected_ftrace_events;
  for (const auto& id_config : configs_) {
    const FtraceConfig& config = id_config.second;
    expected_ftrace_events.insert(config.event_names().begin(),
                                  config.event_names().end());
  }

  std::vector<std::string> events_to_disable =
      difference(current_state_.ftrace_events(), expected_ftrace_events);

  for (auto& name : events_to_disable) {
    const Event* event = table_->GetEventByName(name);
    if (!event)
      continue;
    if (ftrace_->DisableEvent(event->group, event->name))
      current_state_.mutable_ftrace_events()->erase(name);
  }

  if (configs_.empty()) {
    PERFETTO_DCHECK(current_state_.tracing_on());
    ftrace_->DisableTracing();
    ftrace_->SetCpuBufferSizeInPages(0);
    ftrace_->DisableAllEvents();
    ftrace_->ClearTrace();
    current_state_.set_tracing_on(false);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    if (current_state_.atrace_on())
      DisableAtrace();
#endif
  }

  return true;
}

FtraceConfigId FtraceModel::GetNextId() {
  return ++last_id_;
}

const FtraceConfig* FtraceModel::GetConfig(FtraceConfigId id) {
  if (!configs_.count(id))
    return nullptr;
  return &configs_.at(id);
}

void FtraceModel::SetupClock(const FtraceConfig& _) {
  std::string current_clock = ftrace_->GetClock();
  std::set<std::string> clocks = ftrace_->AvailableClocks();

  size_t i = 0;
  const char* clock;
  while ((clock = kClocks[i++])) {
    if (!clocks.count(std::string(clock)))
      continue;
    if (current_clock == clock)
      break;
    ftrace_->SetClock(clock);
    break;
  }
}

void FtraceModel::SetupBufferSize(const FtraceConfig& request) {
  size_t pages = ComputeCpuBufferSizeInPages(request.buffer_size_kb());
  ftrace_->SetCpuBufferSizeInPages(pages);
}

void FtraceModel::EnableAtrace(const FtraceConfig& request) {
  PERFETTO_DCHECK(!current_state_.atrace_on());
  current_state_.set_atrace_on(true);

  PERFETTO_DLOG("Start atrace...");
  std::vector<std::string> args;
  args.push_back("atrace");  // argv0 for exec()
  args.push_back("--async_start");
  for (const auto& category : request.atrace_categories())
    args.push_back(category);
  if (!request.atrace_apps().empty()) {
    args.push_back("-a");
    for (const auto& app : request.atrace_apps())
      args.push_back(app);
  }

  PERFETTO_CHECK(RunAtrace(std::move(args)));
  PERFETTO_DLOG("...done");
}

void FtraceModel::DisableAtrace() {
  PERFETTO_DCHECK(!current_state_.atrace_on());

  PERFETTO_DLOG("Stop atrace...");
  PERFETTO_CHECK(
      RunAtrace(std::vector<std::string>({"atrace", "--async_stop"})));
  PERFETTO_DLOG("...done");

  current_state_.set_atrace_on(false);
}

FtraceState::FtraceState() = default;
FtraceState::~FtraceState() = default;

}  // namespace perfetto
