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

#include <getopt.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/base/utils.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/ipc/service_ipc_host.h"
#include "src/traced/service/service_sandbox.h"

namespace perfetto {

int ServiceMain(int argc, char** argv) {
  int no_sandbox = 0;
  static struct option options[] = {{"no-sandbox", no_argument, &no_sandbox, 1},
                                    {0, 0, 0, 0}};

  for (int narg = 2, ret = 0;
       (ret = getopt_long(argc - 1, &argv[1], "", options, nullptr)) != -1;
       narg++) {
    if (ret == '?') {
      PERFETTO_ELOG("Error on cmdline option: %s", argv[narg]);
      return 1;
    }
  }

  base::UnixTaskRunner task_runner;
  std::unique_ptr<ServiceIPCHost> svc;
  svc = ServiceIPCHost::CreateInstance(&task_runner);

  // When built as part of the Android tree, the two socket are created and
  // bonund by init and their fd number is passed in two env variables.
  // See libcutils' android_get_control_socket().
  const char* env_prod = getenv("ANDROID_SOCKET_traced_producer");
  const char* env_cons = getenv("ANDROID_SOCKET_traced_consumer");
  PERFETTO_CHECK((!env_prod && !env_prod) || (env_prod && env_cons));
  if (env_prod) {
    base::ScopedFile producer_fd(atoi(env_prod));
    base::ScopedFile consumer_fd(atoi(env_cons));
    svc->Start(std::move(producer_fd), std::move(consumer_fd));
  } else {
    unlink(PERFETTO_PRODUCER_SOCK_NAME);
    unlink(PERFETTO_CONSUMER_SOCK_NAME);
    svc->Start(PERFETTO_PRODUCER_SOCK_NAME, PERFETTO_CONSUMER_SOCK_NAME);
  }

  PERFETTO_ILOG("Started traced, listening on %s %s",
                PERFETTO_PRODUCER_SOCK_NAME, PERFETTO_CONSUMER_SOCK_NAME);

  if (!no_sandbox) {
#if PERFETTO_SERVICE_SANDBOX_SUPPORTED()
    InitServiceSandboxOrDie();
#else
    PERFETTO_LOG("Skipping BPF sandbox because not supported on this arch");
#endif
  } else {
    PERFETTO_LOG("Skipping BPF sandbox because of --no-sandbox");
  }

  task_runner.Run();
  return 0;
}  // namespace perfetto

}  // namespace perfetto
