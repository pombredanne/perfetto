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
#include <stdio.h>
#include <string.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/traced/traced.h"

int main(int argc, char** argv) {
  int no_sandbox = 0;
  static struct option options[] = {{"no-sandbox", no_argument, &no_sandbox, 1},
                                    {nullptr, 0, 0, 0}};

  for (int narg = 2, ret = 0;
       (ret = getopt_long(argc - 1, &argv[1], "", options, nullptr)) != -1;
       narg++) {
    if (ret == '?') {
      PERFETTO_ELOG("Error on cmdline option: %s", argv[narg]);
      return 1;
    }
  }

#if BUILDFLAG(HAVE_BPF_SANDBOX)
#if !BUILDFLAG(SANITIZERS)
  no_sandbox = 1;
  PERFETTO_LOG("Skipping BPF sandbox because of sanitizers");
#else
  if (no_sandbox)
    PERFETTO_LOG("Skipping BPF sandbox because of --no-sandbox");
#endif  // BUILDFLAG(SANITIZERS)
#else
  PERFETTO_LOG("Skipping BPF sandbox because not supported on this arch");
#endif  // BUILDFLAG(HAVE_BPF_SANDBOX)

  if (argc > 1 && !strcmp(argv[1], "probes"))
    return perfetto::ProbesMain(no_sandbox);

  if (argc > 1 && !strcmp(argv[1], "service"))
    return perfetto::ServiceMain(no_sandbox);

  printf("Usage: %s probes | service [--no-sandbox]\n", argv[0]);
  return 1;
}
