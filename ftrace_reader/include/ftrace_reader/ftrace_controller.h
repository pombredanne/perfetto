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

#ifndef FTRACE_READER_FTRACE_CONTROLLER_H_
#define FTRACE_READER_FTRACE_CONTROLLER_H_

#include <unistd.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/scoped_file.h"
#include "base/task_runner.h"
#include "base/weak_ptr.h"
#include "gtest/gtest_prod.h"
#include "protozero/protozero_message_handle.h"

#include "protos/ftrace/ftrace_event_bundle.pbzero.h"

namespace perfetto {

class FtraceController;
class ProtoTranslationTable;
class CpuReader;
class FtraceApi;
class EventFilter;

class FtraceConfig {
 public:
  FtraceConfig();
  FtraceConfig(std::set<std::string> events);
  ~FtraceConfig();

  void AddEvent(const std::string&);

  const std::set<std::string>& events() const { return events_; }

 private:
  std::set<std::string> events_;
};

// To consume ftrace data clients implement a |FtraceSink::Delegate| and use it
// to create a |FtraceSink|. While the FtraceSink lives FtraceController will
// call |GetBundleForCpu|, write data into the bundle then call
// |OnBundleComplete| allowing the client to perform finalization.
class FtraceSink {
 public:
  class Delegate {
   public:
    virtual protozero::ProtoZeroMessageHandle<pbzero::FtraceEventBundle>
        GetBundleForCpu(size_t) = 0;
    virtual void OnBundleComplete(
        size_t,
        protozero::ProtoZeroMessageHandle<pbzero::FtraceEventBundle>) = 0;
    virtual ~Delegate() = default;
  };

  FtraceSink(base::WeakPtr<FtraceController>,
             std::unique_ptr<EventFilter>,
             Delegate*);
  ~FtraceSink();

 private:
  friend FtraceController;

  EventFilter* GetEventFilter() { return filter_.get(); }
  protozero::ProtoZeroMessageHandle<pbzero::FtraceEventBundle> GetBundleForCpu(
      size_t cpu) {
    return delegate_->GetBundleForCpu(cpu);
  }
  void OnBundleComplete(
      size_t cpu,
      protozero::ProtoZeroMessageHandle<pbzero::FtraceEventBundle> bundle) {
    delegate_->OnBundleComplete(cpu, std::move(bundle));
  }

  const std::set<std::string>& enabled_events();
  base::WeakPtr<FtraceController> controller_weak_;
  std::unique_ptr<EventFilter> filter_;
  FtraceSink::Delegate* delegate_;
};

// Utility class for controlling ftrace.
class FtraceController {
 public:
  static std::unique_ptr<FtraceController> Create(base::TaskRunner*);
  virtual ~FtraceController();

  std::unique_ptr<FtraceSink> CreateSink(FtraceConfig, FtraceSink::Delegate*);

  void Start();
  void Stop();

 protected:
  // Protected for testing.
  FtraceController(std::unique_ptr<FtraceApi>,
                   base::TaskRunner*,
                   std::unique_ptr<ProtoTranslationTable>);

 private:
  friend FtraceSink;
  FRIEND_TEST(FtraceControllerIntegrationTest, EnableDisableEvent);

  FtraceController(const FtraceController&) = delete;
  FtraceController& operator=(const FtraceController&) = delete;

  void Register(FtraceSink*);
  void Unregister(FtraceSink*);
  void RegisterForEvent(const std::string& event_name);
  void UnregisterForEvent(const std::string& event_name);

  void CpuReady(size_t cpu);

  // Returns a cached CpuReader for |cpu|.
  // CpuReaders are constructed lazily.
  CpuReader* GetCpuReader(size_t cpu);

  std::unique_ptr<FtraceApi> ftrace_api_;
  bool running_ = false;
  base::TaskRunner* task_runner_;
  base::WeakPtrFactory<FtraceController> weak_factory_;
  std::vector<size_t> enabled_count_;
  std::unique_ptr<ProtoTranslationTable> table_;
  std::map<size_t, std::unique_ptr<CpuReader>> readers_;
  std::set<FtraceSink*> sinks_;
  PERFETTO_THREAD_CHECKER(thread_checker_)
};

}  // namespace perfetto

#endif  // FTRACE_READER_FTRACE_CONTROLLER_H_
