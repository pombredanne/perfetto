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

#ifndef INCLUDE_PERFETTO_BASE_BUILD_CONFIG_H_
#define INCLUDE_PERFETTO_BASE_BUILD_CONFIG_H_

// DO NOT include this file in public headers (include/) to avoid collisions.

// Allows to define build flags that give a compiler error if the header that
// defined the flag is not included, instead of silently ignoring the #if block.
#define BUILDFLAG_CAT_INDIRECT(a, b) a##b
#define BUILDFLAG_CAT(a, b) BUILDFLAG_CAT_INDIRECT(a, b)
#define BUILDFLAG(flag) (BUILDFLAG_CAT(BUILDFLAG_DEFINE_, flag)())

#if defined(ANDROID)
#define BUILDFLAG_DEFINE_OS_ANDROID() 1
#define BUILDFLAG_DEFINE_OS_MACOSX() 0
#define BUILDFLAG_DEFINE_OS_LINUX() 0
#elif defined(__APPLE__)
#define BUILDFLAG_DEFINE_OS_ANDROID() 0
#define BUILDFLAG_DEFINE_OS_MACOSX() 1
#define BUILDFLAG_DEFINE_OS_LINUX() 0
#elif defined(__linux__)
#define BUILDFLAG_DEFINE_OS_ANDROID() 0
#define BUILDFLAG_DEFINE_OS_MACOSX() 0
#define BUILDFLAG_DEFINE_OS_LINUX() 1
#else
#error OS not supported (see build_config.h)
#endif

#if BUILDFLAG(OS_ANDROID)
#define BUILDFLAG_DEFINE_HAVE_MEMFD() 1
#else
#define BUILDFLAG_DEFINE_HAVE_MEMFD() 0
#endif

#if (BUILDFLAG(OS_ANDROID) || BUILDFLAG(OS_LINUX)) &&                \
    (defined(__i386__) || defined(__x86_64__) || defined(__arm__) || \
     defined(__aarch64__))
#define BUILDFLAG_DEFINE_HAVE_BPF_SANDBOX() 1
#else
#define BUILDFLAG_DEFINE_HAVE_BPF_SANDBOX() 0
#endif

#if defined(ADDRESS_SANITIZER) || defined(LEAK_SANITIZER) ||  \
    defined(THREAD_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(UNDEFINED_SANITIZER)
#define BUILDFLAG_DEFINE_SANITIZERS() 1
#else
#define BUILDFLAG_DEFINE_SANITIZERS() 0
#endif
#endif  // INCLUDE_PERFETTO_BASE_BUILD_CONFIG_H_
