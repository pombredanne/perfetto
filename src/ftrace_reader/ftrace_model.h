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

#ifndef SRC_FTRACE_READER_FTRACE_MODEL_H_
#define SRC_FTRACE_READER_FTRACE_MODEL_H_

#include "ftrace_procfs.h"
#include "perfetto/ftrace_reader/ftrace_controller.h"

namespace perfetto {

class FtraceState {
 public:
  FtraceState();
  virtual ~FtraceState();

  void set_tracing_on(bool enabled) { tracing_on_ = enabled; }
  bool tracing_on() const { return tracing_on_; }

  void set_atrace_on(bool enabled) { atrace_on_ = enabled; }
  bool atrace_on() const { return atrace_on_; }

  void set_cpu_buffer_size_pages(size_t sz) { cpu_buffer_size_pages_ = sz; }
  size_t cpu_buffer_size_pages() const { return cpu_buffer_size_pages_; }

  void set_ftrace_events(std::set<std::string> events) {
    ftrace_events_ = events;
  }
  const std::set<std::string>& ftrace_events() const { return ftrace_events_; }
  std::set<std::string>* mutable_ftrace_events() { return &ftrace_events_; }

  void set_atrace_categories(std::set<std::string> events) {
    atrace_categories_ = events;
  }
  const std::set<std::string>& atrace_categories() const {
    return atrace_categories_;
  }

  void set_atrace_apps(std::set<std::string> events) { atrace_apps_ = events; }
  const std::set<std::string>& atrace_apps() const { return atrace_apps_; }

 private:
  std::set<std::string> ftrace_events_;
  std::set<std::string> atrace_categories_;
  std::set<std::string> atrace_apps_;

  bool tracing_on_ = false;
  bool atrace_on_ = false;
  size_t cpu_buffer_size_pages_ = 0;
};

class FtraceModel {
 public:
  FtraceModel(FtraceProcfs* ftrace, const ProtoTranslationTable* table);
  virtual ~FtraceModel();

  FtraceConfigId RequestConfig(const FtraceConfig& request);
  bool RemoveConfig(FtraceConfigId id);

  // public for testing
  void SetupClockForTesting(const FtraceConfig& request) {
    SetupClock(request);
  }

  const FtraceConfig* GetConfig(FtraceConfigId id);

 private:
  FtraceModel(const FtraceModel&) = delete;
  FtraceModel& operator=(const FtraceModel&) = delete;

  void SetupClock(const FtraceConfig& request);
  void SetupBufferSize(const FtraceConfig& request);
  void EnableAtrace(const FtraceConfig& request);
  void DisableAtrace();

  FtraceConfigId GetNextId();

  FtraceConfigId last_id_ = 1;
  FtraceProcfs* ftrace_;
  const ProtoTranslationTable* table_;

  FtraceState current_state_;
  std::map<FtraceConfigId, FtraceConfig> configs_;
};

std::set<std::string> GetFtraceEvents(const FtraceConfig& request);
size_t ComputeCpuBufferSizeInPages(size_t requested_buffer_size_kb);

}  // namespace perfetto

#endif  // SRC_FTRACE_READER_FTRACE_MODEL_H_
