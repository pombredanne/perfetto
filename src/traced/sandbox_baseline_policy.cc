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

#include "src/traced/sandbox_baseline_policy.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "src/sandbox/bpf_sandbox.h"

namespace perfetto {

namespace {

constexpr auto kNot = BpfSandbox::kNot;
const uint32_t kMaxMmapSize = 1024 * 1024 * 1024ul;
constexpr auto kProtRW = static_cast<uint32_t>(PROT_READ | PROT_WRITE);

#define MMAP_ARG_FILTERS                                           \
  {0, BPF_JEQ, 0},                   /* |addr| must be nullptr. */ \
      {kNot, BPF_JGT, kMaxMmapSize}, /* No ridiculous lengths */   \
      {kNot, BPF_JSET, ~kProtRW},    /* Only R/W, no PROT_EXEC */  \
      {kNot, BPF_JSET, static_cast<uint32_t>(MAP_FIXED)}, /* No MAP_FIXED */

constexpr BpfSandbox::SyscallFilter kBaselineSandboxPolicy[] = {
    // Syscalls required by base::TaskRunner.
    {SYS_clock_getres, {}},     //
    {SYS_clock_gettime, {}},    //
    {SYS_clock_nanosleep, {}},  //
    {SYS_gettimeofday, {}},     //
    {SYS_nanosleep, {}},        //
    {SYS_ppoll, {}},            //
#if defined(SYS_poll)
    {SYS_poll, {}},
#endif

    // Read/Write/Stat family. Doesn't include open() deliberately.
    {SYS_close, {}},   //
    {SYS_lseek, {}},   //
    {SYS_read, {}},    //
    {SYS_readv, {}},   //
    {SYS_write, {}},   //
    {SYS_writev, {}},  //
#if defined(SYS_stat)
    {SYS_stat, {}},       //
    {SYS_fstat, {}},      //
    {SYS_lstat, {}},      //
    {SYS_ftruncate, {}},  //
#endif
#if defined(SYS_stat64)
    {SYS_stat64, {}},       //
    {SYS_fstat64, {}},      //
    {SYS_lstat64, {}},      //
    {SYS_ftruncate64, {}},  //
#endif

// Mmap family used by allocators. Allow only non-executable mappings.
#if defined(SYS_mmap)
    {SYS_mmap, {MMAP_ARG_FILTERS}},  //
#endif
#if defined(SYS_mmap2)
    {SYS_mmap2, {MMAP_ARG_FILTERS}},  //
#endif
    {SYS_munmap, {}},  //
    {SYS_mprotect,
     {
         {0, BPF_JGT, 0},          // |addr| must be > 0
         {},                       // |len|
         {0, BPF_JEQ, PROT_NONE},  // Allow only PROT_NONE.
     }},
    {SYS_mremap,
     {
         {0, BPF_JGT, 0},                 // |addr| must be > 0
         {},                              // |old_size|
         {kNot, BPF_JGT, kMaxMmapSize},   // |new_size|
         {kNot, BPF_JSET, MREMAP_FIXED},  // Disallow MREMAP_FIXED
     }},
    {SYS_madvise, {}},  //

    // Minimal send/recv network operations. Doesn't include connect/bind/listen
    // deliberately. Those are added separetely in the specialized policy.
    {SYS_socket, {}},       //
    {SYS_sendmsg, {}},      //
    {SYS_recvmsg, {}},      //
    {SYS_shutdown, {}},     //
    {SYS_getsockname, {}},  //
    {SYS_setsockopt, {}},   //
    {SYS_getsockopt, {}},   //

    // Misc syscalls used by Android's liblog and libc.
    {SYS_getpid, {}},   //
    {SYS_getuid, {}},   //
    {SYS_geteuid, {}},  //
    {SYS_getgid, {}},   //
    {SYS_gettid, {}},   //
#if defined(SYS_getuid32)
    {SYS_getuid32, {}},   //
    {SYS_geteuid32, {}},  //
    {SYS_getgid32, {}},   //
#endif
    {SYS_futex, {}},
    {SYS_exit, {}},
    {SYS_exit_group, {}},
    {SYS_kill, {{0, BPF_JEQ, 0}}},  // Allow only signals to self.

    // Allow some fcntl() for setting flags and setting O_CLOEXEC.
    // Used in various places (UnixSocket, TaskRunner).
    {SYS_fcntl, {{}, {0, BPF_JEQ, F_GETFL}}},                            //
    {SYS_fcntl, {{}, {0, BPF_JEQ, F_SETFL}}},                            //
    {SYS_fcntl, {{}, {0, BPF_JEQ, F_SETFD}, {0, BPF_JEQ, FD_CLOEXEC}}},  //
#if defined(SYS_fcntl64)
    // These are only available on 32-bit archs where sizeof(int) == 4.
    {SYS_fcntl64, {{}, {0, BPF_JEQ, F_GETFL}}},
    {SYS_fcntl64, {{}, {0, BPF_JEQ, F_SETFL}}},
    {SYS_fcntl64, {{}, {0, BPF_JEQ, F_SETFD}, {0, BPF_JEQ, FD_CLOEXEC}}},
#endif  // SYS_fcntl64
};

}  // namespace

void EnableBaselineSandboxPolicy(BpfSandbox* sandbox) {
  sandbox->Allow(kBaselineSandboxPolicy);
}

}  // namespace perfetto
