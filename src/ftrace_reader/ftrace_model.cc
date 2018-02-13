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

}  // namespace

FtraceState ComputeFtraceState(std::set<const FtraceConfig*> configs) {
  FtraceState state;

  if (configs.empty()) {
    state.set_ftrace_on(false);
    state.set_cpu_buffer_size_pages(0);
    return state;
  }

  state.set_ftrace_on(true);

  size_t max_buffer_size_kb = 0;
  for (const FtraceConfig* config : configs)
    max_buffer_size_kb =
        std::max<size_t>(config->buffer_size_kb(), max_buffer_size_kb);
  state.set_cpu_buffer_size_pages(
      ComputeCpuBufferSizeInPages(max_buffer_size_kb));

  std::set<std::string> ftrace_events;
  for (const FtraceConfig* config : configs)
    ftrace_events.insert(config->event_names().begin(),
                         config->event_names().end());
  state.set_ftrace_events(std::move(ftrace_events));

  std::set<std::string> atrace_categories;
  for (const FtraceConfig* config : configs)
    atrace_categories.insert(config->atrace_categories().begin(),
                             config->atrace_categories().end());
  state.set_atrace_categories(std::move(atrace_categories));

  std::set<std::string> atrace_apps;
  for (const FtraceConfig* config : configs)
    atrace_apps.insert(config->atrace_apps().begin(),
                       config->atrace_apps().end());
  state.set_atrace_apps(std::move(atrace_apps));

  return state;
}

FtraceModel::FtraceModel(FtraceProcfs* ftrace,
                         const ProtoTranslationTable* table)
    : ftrace_(ftrace), table_(table), current_state_(){};
FtraceModel::~FtraceModel() = default;

bool FtraceModel::AddConfig(const FtraceConfig* config) {
  configs_.insert(config);
  bool result = Update();
  if (!result)
    configs_.erase(config);
  return result;
}

bool FtraceModel::RemoveConfig(const FtraceConfig* config) {
  size_t removed = configs_.erase(config);
  PERFETTO_DCHECK(removed == 1);
  Update();
  return true;
}

bool FtraceModel::Update() {
  FtraceState ideal_state = ComputeFtraceState(configs_);

  bool is_ftrace_enabled = ftrace_->IsTracingEnabled();
  bool switching_tracing = false;

  if (current_state_.ftrace_on() != ideal_state.ftrace_on()) {
    // If someone else is using ftrace give up now.
    if (is_ftrace_enabled != current_state_.ftrace_on())
      return false;

    switching_tracing = true;
  }

  // If we're about to turn tracing on use this opportunity to set up the
  // clock:
  if (switching_tracing && ideal_state.ftrace_on())
    SetupClock();

  // Changing the buffer size clears the buffer so it's not worth it if we're
  // already tracing.
  if (switching_tracing && ideal_state.ftrace_on()) {
    if (current_state_.cpu_buffer_size_pages() !=
        ideal_state.cpu_buffer_size_pages())
      ftrace_->SetCpuBufferSizeInPages(ideal_state.cpu_buffer_size_pages());
  }

  std::vector<std::string> events_to_enable =
      difference(ideal_state.ftrace_events(), current_state_.ftrace_events());

  std::vector<std::string> events_to_disable =
      difference(current_state_.ftrace_events(), ideal_state.ftrace_events());

  for (auto& name : events_to_enable) {
    const Event* event = table_->GetEventByName(name);
    if (!event) {
      PERFETTO_DLOG("Can't enable %s, event not known", name.c_str());
      continue;
    }
    if (ftrace_->EnableEvent(event->group, event->name))
      current_state_.mutable_ftrace_events()->insert(name);
  }

  for (auto& name : events_to_disable) {
    const Event* event = table_->GetEventByName(name);
    if (!event)
      continue;
    if (ftrace_->DisableEvent(event->group, event->name))
      current_state_.mutable_ftrace_events()->erase(name);
  }

  if (switching_tracing) {
    ftrace_->SetTracingOn(ideal_state.ftrace_on());
    current_state_.set_ftrace_on(ideal_state.ftrace_on());
  }

  // If we just turned tracing off lets take this opportunity to clean up:
  if (switching_tracing && !ideal_state.ftrace_on()) {
    ftrace_->SetCpuBufferSizeInPages(0);
    ftrace_->DisableAllEvents();
    ftrace_->ClearTrace();
  }

  return true;
}

void FtraceModel::SetupClock() {
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

FtraceState::FtraceState() = default;
FtraceState::~FtraceState() = default;

}  // namespace perfetto
