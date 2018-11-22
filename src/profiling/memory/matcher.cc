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

#include "src/profiling/memory/matcher.h"

#include "perfetto/base/logging.h"

namespace perfetto {
namespace profiling {

Matcher::ProcessHandle::ProcessHandle(Matcher* matcher, pid_t pid)
    : matcher_(matcher), pid_(pid) {}

Matcher::ProcessHandle::ProcessHandle(ProcessHandle&& other)
    : matcher_(other.matcher_), pid_(other.pid_) {
  other.matcher_ = nullptr;
}

Matcher::ProcessHandle& Matcher::ProcessHandle::operator=(
    ProcessHandle&& other) {
  matcher_ = other.matcher_;
  pid_ = other.pid_;
  other.matcher_ = nullptr;
  return *this;
}

Matcher::ProcessHandle::~ProcessHandle() {
  if (matcher_)
    matcher_->RemoveProcess(pid_);
}

Matcher::ProcessSetHandle::ProcessSetHandle(
    Matcher* matcher,
    HeapprofdProducer::DataSource* data_source)
    : matcher_(matcher), data_source_(data_source) {}

Matcher::ProcessSetHandle::ProcessSetHandle(ProcessSetHandle&& other)
    : matcher_(other.matcher_), data_source_(other.data_source_) {
  other.matcher_ = nullptr;
}

Matcher::ProcessSetHandle& Matcher::ProcessSetHandle::operator=(
    ProcessSetHandle&& other) {
  matcher_ = other.matcher_;
  data_source_ = other.data_source_;
  other.matcher_ = nullptr;
  return *this;
}

Matcher::ProcessSetHandle::~ProcessSetHandle() {
  if (matcher_)
    matcher_->UnwaitProcessSet(data_source_);
}

Matcher::Matcher(std::function<void(pid_t)> shutdown_fn,
                 std::function<void(const Process&,
                                    const std::vector<ProcessSet*>&)> match_fn)
    : shutdown_fn_(shutdown_fn),
      match_fn_(match_fn),
      current_orphan_generation_(new ProcessSetItem(this, ProcessSet{})),
      old_orphan_generation_(new ProcessSetItem(this, ProcessSet{})) {}

Matcher::ProcessHandle Matcher::NotifyProcess(Process process) {
  pid_t pid = process.pid;
  decltype(pid_to_process_)::iterator it;
  bool inserted;
  std::tie(it, inserted) = pid_to_process_.emplace(pid, std::move(process));
  if (!inserted) {
    PERFETTO_DFATAL("Duplicated PID");
    return ProcessHandle(nullptr, 0);
  }

  ProcessItem* new_process_item = &(it->second);
  const std::string& cmdline = new_process_item->process.cmdline;
  cmdline_to_process_.emplace(cmdline, new_process_item);

  // Go through existing ProcessSets to find ones containing the newly
  // connected process.
  std::set<ProcessSetItem*> matching_process_set_items = process_set_for_all_;
  auto pid_range = pid_to_process_set_.equal_range(pid);
  for (auto i = pid_range.first; i != pid_range.second; ++i) {
    ProcessSet& ps = i->second->process_set;
    if (ps.pids.find(pid) != ps.pids.end())
      matching_process_set_items.emplace(i->second);
  }
  auto cmdline_range = cmdline_to_process_set_.equal_range(cmdline);
  for (auto i = cmdline_range.first; i != cmdline_range.second; ++i) {
    ProcessSet& ps = i->second->process_set;
    if (ps.process_cmdline.find(cmdline) != ps.process_cmdline.end())
      matching_process_set_items.emplace(i->second);
  }

  bool found_process_set_item = !matching_process_set_items.empty();
  // If we did not find any ProcessSet, we use the placeholder orphan process
  // set. This allows processes to connect before the DataSource was
  // initialized. This happens on user builds for the fork model.
  if (!found_process_set_item)
    matching_process_set_items.emplace(current_orphan_generation_.get());

  for (ProcessSetItem* process_set_item : matching_process_set_items) {
    process_set_item->process_items.emplace(new_process_item);
    new_process_item->references.emplace(process_set_item);
    // TODO(fmayer): New match here.
  }
  if (found_process_set_item)
    RunMatchFn(new_process_item);

  return ProcessHandle(this, pid);
}

void Matcher::RemoveProcess(pid_t pid) {
  auto it = pid_to_process_.find(pid);
  if (it == pid_to_process_.end()) {
    PERFETTO_DFATAL("Could not find process.");
    return;
  }
  ProcessItem& process_item = it->second;
  size_t erased = cmdline_to_process_.erase(process_item.process.cmdline);
  PERFETTO_DCHECK(erased);
  pid_to_process_.erase(it);
}

Matcher::ProcessSetHandle Matcher::AwaitProcessSet(ProcessSet process_set) {
  HeapprofdProducer::DataSource* ds = process_set.data_source;
  decltype(process_sets_)::iterator it;
  bool inserted;
  std::tie(it, inserted) = process_sets_.emplace(
      std::piecewise_construct, std::forward_as_tuple(ds),
      std::forward_as_tuple(this, std::move(process_set)));
  if (!inserted) {
    PERFETTO_DFATAL("Duplicate DataSource");
    return ProcessSetHandle(nullptr, nullptr);
  }
  ProcessSetItem* new_process_set_item = &(it->second);
  const ProcessSet& new_process_set = new_process_set_item->process_set;

  // Go through currently active processes to find ones matching the new
  // ProcessSet.
  std::set<ProcessItem*> matching_process_items;
  if (new_process_set.all) {
    process_set_for_all_.emplace(new_process_set_item);
    for (auto& p : pid_to_process_) {
      ProcessItem& process_item = p.second;
      matching_process_items.emplace(&process_item);
    }
  } else {
    for (pid_t pid : new_process_set.pids) {
      pid_to_process_set_.emplace(pid, new_process_set_item);
      auto process_it = pid_to_process_.find(pid);
      if (process_it != pid_to_process_.end())
        matching_process_items.emplace(&(process_it->second));
    }
    for (std::string cmdline : new_process_set.process_cmdline) {
      cmdline_to_process_set_.emplace(cmdline, new_process_set_item);
      auto process_it = cmdline_to_process_.find(cmdline);
      if (process_it != cmdline_to_process_.end())
        matching_process_items.emplace(process_it->second);
    }
  }

  for (ProcessItem* process_item : matching_process_items) {
    new_process_set_item->process_items.emplace(process_item);
    process_item->references.emplace(new_process_set_item);
    RunMatchFn(process_item);
  }

  return ProcessSetHandle(this, ds);
}

void Matcher::UnwaitProcessSet(HeapprofdProducer::DataSource* ds) {
  auto it = process_sets_.find(ds);
  if (it == process_sets_.end()) {
    PERFETTO_DFATAL("Removing invalid ProcessSet");
    return;
  }

  ProcessSetItem& process_set_item = it->second;
  const ProcessSet& process_set = process_set_item.process_set;

  for (pid_t pid : process_set.pids) {
    auto pid_range = pid_to_process_set_.equal_range(pid);
    for (auto i = pid_range.first; i != pid_range.second;) {
      if (i->second == &process_set_item)
        i = pid_to_process_set_.erase(i);
      else
        ++i;
    }
  }
  for (const std::string& cmdline : process_set.process_cmdline) {
    auto cmdline_range = cmdline_to_process_set_.equal_range(cmdline);
    for (auto i = cmdline_range.first; i != cmdline_range.second;) {
      if (i->second == &process_set_item)
        i = cmdline_to_process_set_.erase(i);
      else
        ++i;
    }
  }

  if (process_set.all)
    process_set_for_all_.erase(&process_set_item);
  process_sets_.erase(it);
}

void Matcher::GarbageCollectOrphans() {
  old_orphan_generation_ = std::move(current_orphan_generation_);
  current_orphan_generation_.reset(new ProcessSetItem(this, ProcessSet{}));
}

Matcher::ProcessItem::~ProcessItem() {
  for (ProcessSetItem* process_set_item : references) {
    size_t erased = process_set_item->process_items.erase(this);
    PERFETTO_DCHECK(erased);
  }
}

Matcher::ProcessSetItem::~ProcessSetItem() {
  for (ProcessItem* process_item : process_items) {
    size_t erased = process_item->references.erase(this);
    PERFETTO_DCHECK(erased);
    if (process_item->references.empty())
      matcher->ShutdownProcess(process_item->process.pid);
  }
}

void Matcher::ShutdownProcess(pid_t pid) {
  shutdown_fn_(pid);
}

void Matcher::RunMatchFn(ProcessItem* process_item) {
  std::vector<ProcessSet*> process_sets;
  for (ProcessSetItem* process_set_item : process_item->references) {
    if (process_set_item != current_orphan_generation_.get() &&
        process_set_item != old_orphan_generation_.get())
      process_sets.emplace_back(&(process_set_item->process_set));
  }
  match_fn_(process_item->process, process_sets);
}

}  // namespace profiling
}  // namespace perfetto
