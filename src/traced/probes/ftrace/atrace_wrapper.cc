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

#include "src/traced/probes/ftrace/atrace_wrapper.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"

namespace perfetto {

namespace {

RunAtraceFunction g_run_atrace_for_testing = nullptr;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
// Args should include "atrace" for argv[0].
bool ExecvAtrace(const std::vector<std::string>& args) {
  int status = 1;

  std::vector<char*> argv;
  // args, and then a null.
  argv.reserve(1 + args.size());
  for (const auto& arg : args)
    argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);

  // Create the pipe for the child process to return stderr.
  int filedes[2];
  if (pipe(filedes) == -1)
    return false;

  pid_t pid = fork();
  PERFETTO_CHECK(pid >= 0);
  if (pid == 0) {
    // Duplicate the write end of the pipe into stderr.
    if ((dup2(filedes[1], STDERR_FILENO) == -1)) {
      const char kError[] = "Unable to duplicate stderr fd";
      write(filedes[1], kError, sizeof(kError));
      _exit(1);
    }

    // Close stdin/out + any file descriptor that we might have mistakenly
    // not marked as FD_CLOEXEC.
    for (int i = 0; i < 128; i++) {
      if (i != STDERR_FILENO)
        close(i);
    }

    // Close the read and write end of the pipe fds.
    close(filedes[1]);
    close(filedes[0]);

    execv("/system/bin/atrace", &argv[0]);
    // Reached only if execv fails.
    _exit(1);
  }

  // Close the write end of the pipe.
  close(filedes[1]);

  // Collect the output from child process.
  std::string error;
  char buffer[4096];

  auto read_fd = filedes[0];
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(read_fd, &rfds);

  auto start = base::GetWallTimeMs();
  constexpr auto timeout = base::TimeMillis(7500);
  auto timeout_spec = base::ToPosixTimespec(timeout);
  while (true) {
    auto ret = pselect(1, &rfds, nullptr, nullptr, &timeout_spec, nullptr);
    if (ret < 0) {
      break;
    } else if (ret > 0) {
      ssize_t count = PERFETTO_EINTR(read(read_fd, buffer, sizeof(buffer)));
      if (count == 0 || count == -1)
        break;
      error.append(buffer, static_cast<size_t>(count));
    }

    auto now = base::GetWallTimeMs();
    if (now - start >= timeout) {
      error.append("Timed out waiting for atrace");
      break;
    }
    timeout_spec = base::ToPosixTimespec(now - start);
  }

  // Close the read end of the pipe.
  close(filedes[0]);

  // Kill the forked process.
  kill(pid, SIGKILL);

  // Wait until the child process exits fully.
  PERFETTO_EINTR(waitpid(pid, &status, 0));

  bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (!ok) {
    // TODO(lalitm): use the stderr result from atrace.
    PERFETTO_ELOG("%s", error.c_str());
  }
  return ok;
}
#endif

}  // namespace

bool RunAtrace(const std::vector<std::string>& args) {
  if (g_run_atrace_for_testing)
    return g_run_atrace_for_testing(args);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  return ExecvAtrace(args);
#else
  PERFETTO_LOG("Atrace only supported on Android.");
  return false;
#endif
}

void SetRunAtraceForTesting(RunAtraceFunction f) {
  g_run_atrace_for_testing = f;
}

}  // namespace perfetto
