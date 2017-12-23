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

#include "src/traced/service/service_sandbox.h"

#if PERFETTO_SERVICE_SANDBOX_SUPPORTED()

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#if BUILDFLAG(OS_ANDROID)
#include <linux/memfd.h>
#endif  // OS_ANDROID
#include "src/sandbox/bpf_sandbox.h"

namespace perfetto {

void InitServiceSandboxOrDie() {
  constexpr auto kNot = BpfSandbox::kNot;
  BpfSandbox bpf;

  // For task runners.
  bpf.Allow(SYS_ppoll);
#if defined(SYS_poll)
  bpf.Allow(SYS_poll);
#endif
  bpf.Allow(SYS_gettimeofday);
  bpf.Allow(SYS_clock_gettime);
  bpf.Allow(SYS_clock_getres);
  bpf.Allow(SYS_nanosleep);
  bpf.Allow(SYS_clock_nanosleep);
  bpf.Allow(SYS_pipe);

  // Restricted mmap/mprotect. Don't allow PROT_EXEC. Used by allocators.
  const uint32_t kProtNotRW = ~(static_cast<uint32_t>(PROT_READ | PROT_WRITE));
  const std::initializer_list<BpfSandbox::ArgMatcher> mmap_filters = {
      {0, BPF_JEQ, 0},                            // |addr| must be nullptr.
      {kNot, BPF_JGT, 2 * 1024 * 1024 * 1024ul},  // no ridiculous len.
      {kNot, BPF_JSET, kProtNotRW},               // No PROT_EXEC.
  };
#if defined(SYS_mmap)
  bpf.Allow(SYS_mmap, mmap_filters);
#endif
#if defined(SYS_mmap2)
  bpf.Allow(SYS_mmap2, mmap_filters);
#endif
  bpf.Allow(SYS_munmap);
  bpf.Allow(SYS_mprotect, {
                              {0, BPF_JGT, 0},  // |addr| must be > 0
                              {},
                              {kNot, BPF_JSET, kProtNotRW},  // No PROT_EXEC
                          });
  bpf.Allow(SYS_mremap, {
                            {0, BPF_JGT, 0},  // |addr| must be > 0
                            {},
                            {},
                            {kNot, BPF_JSET, MREMAP_FIXED},
                        });
  bpf.Allow(SYS_madvise);

  // General I/O: read*/write*/close/*seek*/*stat*. No open().
  bpf.Allow(SYS_read);
  bpf.Allow(SYS_write);
  bpf.Allow(SYS_readv);
  bpf.Allow(SYS_writev);
  bpf.Allow(SYS_lseek);
#if defined(SYS_stat)
  bpf.Allow(SYS_stat);
#endif
#if defined(SYS_stat64)
  bpf.Allow(SYS_stat64);
#endif
#if defined(SYS_fstat)
  bpf.Allow(SYS_fstat);
#endif
#if defined(SYS_fstat64)
  bpf.Allow(SYS_fstat64);
#endif
#if defined(SYS_lstat)
  bpf.Allow(SYS_lstat);
#endif
#if defined(SYS_lstat64)
  bpf.Allow(SYS_lstat64);
#endif
#if defined(SYS_ftruncate)
  bpf.Allow(SYS_ftruncate);
#endif
#if defined(SYS_ftruncate64)
  bpf.Allow(SYS_ftruncate64);
#endif
  bpf.Allow(SYS_close);

  // Networking, for unix sockets. No SYS_connect.
  bpf.Allow(SYS_socket);
  bpf.Allow(SYS_accept);
  bpf.Allow(SYS_accept4);
  bpf.Allow(SYS_sendmsg);
  bpf.Allow(SYS_recvmsg);
  bpf.Allow(SYS_shutdown);
  bpf.Allow(SYS_bind);
  bpf.Allow(SYS_listen);
  bpf.Allow(SYS_getsockname);
  bpf.Allow(SYS_setsockopt);
  bpf.Allow(SYS_getsockopt);

  // Android liblog.
  bpf.Allow(SYS_getpid);
  bpf.Allow(SYS_getuid);
  bpf.Allow(SYS_geteuid);
  bpf.Allow(SYS_getgid);
  bpf.Allow(SYS_gettid);
#if defined(SYS_getuid32)
  bpf.Allow(SYS_getuid32);
  bpf.Allow(SYS_geteuid32);
  bpf.Allow(SYS_getgid32);
#endif
  bpf.Allow(SYS_futex);  // Android libc.so an libunwind use this.
  bpf.Allow(SYS_exit);
  bpf.Allow(SYS_exit_group);

  // Allow some fcntl() for setting flags, setting O_CLOEXEC and applying seals.
  // Used in various places (UnixSocket, TaskRunner).
  bpf.Allow(SYS_fcntl, {{}, {0, BPF_JEQ, F_GETFL}});
  bpf.Allow(SYS_fcntl, {{}, {0, BPF_JEQ, F_SETFL}});
  bpf.Allow(SYS_fcntl, {{}, {0, BPF_JEQ, F_SETFD}, {0, BPF_JEQ, FD_CLOEXEC}});
  bpf.Allow(SYS_fcntl, {{}, {0, BPF_JEQ, F_ADD_SEALS}});

// These are only available on 32-bit archs where sizeof(int) == 4.
#if defined(SYS_fcntl64) && SYS_fcntl64
  bpf.Allow(SYS_fcntl64, {{}, {0, BPF_JEQ, F_GETFL}});
  bpf.Allow(SYS_fcntl64, {{}, {0, BPF_JEQ, F_SETFL}});
  bpf.Allow(SYS_fcntl64, {{}, {0, BPF_JEQ, F_SETFD}, {0, BPF_JEQ, FD_CLOEXEC}});
  bpf.Allow(SYS_fcntl64, {{}, {0, BPF_JEQ, F_ADD_SEALS}});
#endif

  bpf.Allow(SYS_kill, {{0, BPF_JEQ, 0}});  // Only self-signals.
#if BUILDFLAG(OS_ANDROID)
  bpf.Allow(__NR_memfd_create);  // Used to create shmem.
#endif

  bpf.EnterSandboxOrDie();
}

}  // namespace perfetto

#endif  // PERFETTO_SERVICE_SANDBOX_SUPPORTED
