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

#include "src/trace_processor/process_tracker.h"

namespace perfetto {
namespace trace_processor {

ProcessTracker::ProcessTracker(TraceProcessorContext* context)
    : context_(context){};

ProcessTracker::~ProcessTracker() = default;

TraceStorage::UniqueTid ProcessTracker::UpdateThread(
    uint64_t timestamp,
    uint32_t tid,
    TraceStorage::StringId thread_name_id) {
  auto pair_it = tids_.equal_range(tid);

  if (pair_it.first == pair_it.second) {
    // If none exist, assign a new utid and store it.
    TraceStorage::Thread new_thread;
    new_thread.name_id = thread_name_id;
    new_thread.start_ns = timestamp;
    new_thread.upid = 0;
    // thread_count() ignores the first invalid thread - hence '+1'.
    TraceStorage::UniqueTid new_utid = static_cast<TraceStorage::UniqueTid>(
        context_->storage->thread_count() + 1);
    tids_.emplace(tid, new_utid);
    context_->storage->StoreNewThread(new_thread);
    return new_utid;
  }

  // If there is a previous utid for that tid, use that.
  auto prev_utid = std::prev(pair_it.second)->second;
  return prev_utid;
};

TraceStorage::UniqueTid ProcessTracker::UpdateThread(uint32_t tid,
                                                     uint32_t tgid) {
  auto tids_pair = tids_.equal_range(tid);

  // We only care about tids for which we have a matching utid.
  PERFETTO_DCHECK(std::distance(tids_pair.first, tids_pair.second) <= 1);

  if (tids_pair.first != tids_pair.second) {
    PERFETTO_DCHECK(tids_pair.first->second <
                    context_->storage->thread_count() + 1);

    TraceStorage::Thread* thread =
        context_->storage->GetMutableThread(tids_pair.first->second);
    // If no upid is set - look it up.
    if (thread->upid == 0) {
      auto pids_pair = pids_.equal_range(tgid);
      PERFETTO_DCHECK(std::distance(pids_pair.first, pids_pair.second) <= 1);
      if (pids_pair.first != pids_pair.second) {
        thread->upid = pids_pair.first->second;
        // If this is the first time we've used this process, set start_ns.
        TraceStorage::Process* process =
            context_->storage->GetMutableProcess(pids_pair.first->second);
        if (process->start_ns == 0)
          process->start_ns = thread->start_ns;
      }
    }
  }
  return tids_pair.first->second;
}

TraceStorage::UniquePid ProcessTracker::UpdateProcess(uint32_t pid,
                                                      const char* process_name,
                                                      size_t process_name_len) {
  auto pids_pair = pids_.equal_range(pid);
  auto proc_name_id =
      context_->storage->InternString(process_name, process_name_len);

  // We only create a new upid if there isn't one for that pid.
  if (pids_pair.first == pids_pair.second) {
    TraceStorage::UniquePid new_upid = static_cast<TraceStorage::UniquePid>(
        context_->storage->process_count() + 1);
    pids_.emplace(pid, new_upid);
    TraceStorage::Process new_process;
    new_process.name_id = proc_name_id;
    context_->storage->StoreNewProcess(new_process);
    return new_upid;
  }
  auto prev_utid = std::prev(pids_pair.second)->second;
  return prev_utid;
}

ProcessTracker::UniqueProcessRange ProcessTracker::UpidsForPid(uint32_t pid) {
  return pids_.equal_range(pid);
}

ProcessTracker::UniqueThreadRange ProcessTracker::UtidsForTid(uint32_t tid) {
  return tids_.equal_range(tid);
}

}  // namespace trace_processor
}  // namespace perfetto
