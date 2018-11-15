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

#ifndef SRC_TRACED_PROBES_FTRACE_FTRACE_THREAD_SYNC_H_
#define SRC_TRACED_PROBES_FTRACE_FTRACE_THREAD_SYNC_H_

#include <stdint.h>

#include <bitset>
#include <mutex>
#include <condition_variable>

#include "perfetto/base/utils.h"

namespace perfetto {

// This struct is acessed both by the FtraceController on the main thread and
// by the CpuReader(s) on their worker threads. It is used to synchronize
// handshakes between FtraceController and CpuReader(s). There is only *ONE*
// instance of this state, owned by the FtraceController and shared with all
// CpuReader(s).
struct FtraceThreadSync {
  // Mutex & condition variable shared by all threads. All variables below
  // are read and modified only under |mutex|.
  std::mutex mutex;
  std::condition_variable cond;

  // These variables are written only by FtraceController. On each cycle,
  // FtraceController increases the |cmd_id| monotonic counter and issues the
  // new command. |cmd_id| is used by the CpuReader(s) to distinguish a new
  // command from a spurious wakeup.
  enum Cmd { kRun = 0, kFlush, kQuit };
  Cmd cmd = kRun;
  uint64_t cmd_id = 0;

  // Incremented by the FtraceController every time ftrace is re-started.
  uint64_t generation = 0;

  // This bitmap is cleared by the FtraceController before every kRun command
  // and is optionally set by OnDataAvailable() if a CpuReader did fetch any
  // ftrace data during the read cycle.
  std::bitset<base::kMaxCpus> cpus_to_drain;

  // This is set to 0 by the FtraceController before issuing a kFlush command
  // and is increased by each CpuReader after they have completed the flush.
  int flush_acks = 0;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FTRACE_FTRACE_THREAD_SYNC_H_
