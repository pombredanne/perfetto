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

#ifndef SRC_TRACED_SERVICE_SERVICE_SANDBOX_H_
#define SRC_TRACED_SERVICE_SERVICE_SANDBOX_H_

#include "perfetto/base/build_config.h"

#if (BUILDFLAG(OS_ANDROID) || BUILDFLAG(OS_LINUX)) &&                \
    (defined(__i386__) || defined(__x86_64__) || defined(__arm__) || \
     defined(__aarch64__))
#define PERFETTO_PROBES_SANDBOX_SUPPORTED() 1
#else
#define PERFETTO_PROBES_SANDBOX_SUPPORTED() 0
#endif

namespace perfetto {

void InitProbesSandboxOrDie();

}  // namespace perfetto

#endif  // SRC_TRACED_SERVICE_SERVICE_SANDBOX_H_
