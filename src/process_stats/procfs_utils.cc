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

#include "src/process_stats/procfs_utils.h"

#include <stdio.h>
#include <string.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/string_splitter.h"
#include "src/process_stats/file_utils.h"

using file_utils::ForEachPidInProcPath;
using file_utils::ReadProcFile;
using file_utils::ReadProcFileTrimmed;

namespace procfs_utils {

namespace {

constexpr const char kJavaAppPrefix[] = "/system/bin/app_process";
constexpr const char kZygotePrefix[] = "zygote";

inline void ReadProcString(int pid, const char* path, char* buf, size_t size) {
  if (!file_utils::ReadProcFileTrimmed(pid, path, buf, size))
    buf[0] = '\0';
}

inline void ReadExePath(int pid, char* buf, size_t size) {
  char exe_path[64];
  sprintf(exe_path, "/proc/%d/exe", pid);
  ssize_t res = readlink(exe_path, buf, size - 1);
  if (res >= 0)
    buf[res] = '\0';
  else
    buf[0] = '\0';
}

inline bool IsApp(const char* name, const char* exe) {
  return strncmp(exe, kJavaAppPrefix, sizeof(kJavaAppPrefix) - 1) == 0 &&
         strncmp(name, kZygotePrefix, sizeof(kZygotePrefix) - 1) != 0;
}

inline int ReadStatusLine(const char* buf, const char* status_string) {
  const char* line = strstr(buf, status_string);
  if (!line) {
    PERFETTO_DCHECK(false);
    return 0;
  }
  return atoi(line + strlen(status_string));
}

}  // namespace

bool ReadProcessInfo(int pid, ProcessInfo* process, bool stop_recursion) {
  char proc_status[512];
  ssize_t rsize = ReadProcFile(pid, "status", proc_status, sizeof(proc_status));
  if (rsize <= 0)
    return false;

  int tgid = ReadStatusLine(proc_status, "\nTgid:");
  if (tgid <= 0)
    return false;

  if (tgid != pid) {
    if (stop_recursion) {
      PERFETTO_DCHECK(false);
      return false;
    }
    // This is a thread ID. Just read the actual process ID.
    return ReadProcessInfo(tgid, process, true);
  }

  process->pid = pid;
  // It's not enough to just null terminate this since cmdline uses null as
  // the argument seperator:
  char cmdline_buf[256]{};
  ReadProcString(pid, "cmdline", cmdline_buf, sizeof(cmdline_buf));
  if (cmdline_buf[0] == 0) {
    // Nothing in cmdline_buf so read name from /comm instead.
    char name[256];
    ReadProcString(pid, "comm", name, sizeof(name));
    process->cmdline.push_back(name);
    process->in_kernel = true;
  } else {
    using perfetto::base::StringSplitter;
    for (StringSplitter ss(cmdline_buf, sizeof(cmdline_buf), '\0'); ss.Next();)
      process->cmdline.push_back(ss.cur_token());
    ReadExePath(pid, process->exe, sizeof(process->exe));
    process->is_app = IsApp(process->cmdline[0].c_str(), process->exe);
  }
  process->ppid = ReadStatusLine(proc_status, "\nPPid:");

  // Don't attempt to read child threads from kernel threads.
  if (process->in_kernel)
    return true;

  char tasks_path[64];
  sprintf(tasks_path, "/proc/%d/task", process->pid);
  ForEachPidInProcPath(tasks_path, [process](int tid) {
    if (process->threads.count(tid))
      return;
    ThreadInfo thread;
    thread.tid = tid;
    char task_comm[64];
    sprintf(task_comm, "task/%d/comm", tid);
    ReadProcString(process->pid, task_comm, thread.name, sizeof(thread.name));
    if (thread.name[0] == '\0' && process->is_app)
      strcpy(thread.name, "UI Thread");
    process->threads[tid] = thread;
  });

  return true;
}

}  // namespace procfs_utils
