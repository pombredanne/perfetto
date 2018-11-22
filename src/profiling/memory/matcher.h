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

#ifndef SRC_PROFILING_MEMORY_MATCHER_H_
#define SRC_PROFILING_MEMORY_MATCHER_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/profiling/memory/heapprofd_producer.h"

namespace perfetto {
namespace profiling {

class Matcher;

struct Process {
  friend class Matcher;
  pid_t pid;
  std::string cmdline;
};

struct ProcessSet {
  friend class Matcher;
  HeapprofdProducer::DataSource* data_source;
  std::set<pid_t> pids;
  std::set<std::string> process_cmdline;
  bool all = false;

  bool operator<(const ProcessSet& other);
};

class Matcher {
 public:
  class ProcessHandle {
   public:
    friend class Matcher;
    ~ProcessHandle();

   private:
    ProcessHandle(Matcher* matcher, pid_t pid);

    Matcher* matcher_;
    pid_t pid_;
  };

  class ProcessSetHandle {
   public:
    friend class Matcher;
    ~ProcessSetHandle();

   private:
    ProcessSetHandle(Matcher* matcher,
                     HeapprofdProducer::DataSource* data_source);

    Matcher* matcher_;
    HeapprofdProducer::DataSource* data_source_;
  };

  Matcher(std::function<void(pid_t)> shutdown_fn,
          std::function<void(const Process&, const std::vector<ProcessSet*>&)>
              match_fn);
  ProcessHandle NotifyProcess(Process process);
  ProcessSetHandle AwaitProcessSet(ProcessSet process_set);

  void GarbageCollectOrphans();

 private:
  struct ProcessSetItem;
  struct ProcessItem {
    // No copy or move as we rely on pointer stability in ProcessSetItem.
    ProcessItem(const ProcessItem&) = delete;
    ProcessItem& operator=(const ProcessItem&) = delete;
    ProcessItem(ProcessItem&&) = delete;
    ProcessItem& operator=(ProcessItem&&) = delete;

    ProcessItem(Process p) : process(std::move(p)) {}

    Process process;
    std::set<ProcessSetItem*> references;

    ~ProcessItem();
  };

  struct ProcessSetItem {
    // No copy or move as we rely on pointer stability in ProcessSet.
    ProcessSetItem(const ProcessSetItem&) = delete;
    ProcessSetItem& operator=(const ProcessSetItem&) = delete;
    ProcessSetItem(ProcessSetItem&&) = delete;
    ProcessSetItem& operator=(ProcessSetItem&&) = delete;

    ProcessSetItem(Matcher* m, ProcessSet ps)
        : matcher(m), process_set(std::move(ps)) {}

    Matcher* matcher;
    ProcessSet process_set;
    std::set<ProcessItem*> process_items;

    ~ProcessSetItem();
  };

  void UnwaitProcessSet(HeapprofdProducer::DataSource* ds);
  void RemoveProcess(pid_t pid);
  void ShutdownProcess(pid_t pid);
  void RunMatchFn(ProcessItem* process_item);

  std::function<void(pid_t)> shutdown_fn_;
  std::function<void(const Process&, const std::vector<ProcessSet*>&)>
      match_fn_;

  // TODO(fmayer): dtor order.
  std::map<pid_t, ProcessItem> pid_to_process_;
  std::map<std::string, ProcessItem*> cmdline_to_process_;

  std::map<HeapprofdProducer::DataSource*, ProcessSetItem> process_sets_;
  std::multimap<pid_t, ProcessSetItem*> pid_to_process_set_;
  std::multimap<std::string, ProcessSetItem*> cmdline_to_process_set_;
  std::set<ProcessSetItem*> process_set_for_all_;

  std::unique_ptr<ProcessSetItem> current_orphan_generation_;
  std::unique_ptr<ProcessSetItem> old_orphan_generation_;
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_MATCHER_H_
