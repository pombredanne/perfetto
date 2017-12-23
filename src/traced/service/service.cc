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

#include "perfetto/base/build_config.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/base/utils.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/ipc/service_ipc_host.h"

#if BUILDFLAG(OS_ANDROID) || BUILDFLAG(OS_LINUX)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#if BUILDFLAG(OS_ANDROID)
#include <linux/memfd.h>
#endif
#include "src/sandbox/bpf_sandbox.h"
#endif

namespace perfetto {

namespace {

void InitServiceSandboxIfSupported() {
#if BUILDFLAG(OS_ANDROID) || BUILDFLAG(OS_LINUX)
  constexpr auto kNot = BpfSandbox::kNot;
  BpfSandbox bpf(SECCOMP_RET_TRAP);
  bpf.Allow(SYS_ppoll);          // For task runners.
  bpf.Allow(SYS_poll);           // For task runners.
  bpf.Allow(SYS_gettimeofday);   // For task runners.
  bpf.Allow(SYS_clock_gettime);  // For task runners.
  bpf.Allow(SYS_clock_getres);   // For task runners.
  const uint32_t kProtNotRW = ~(static_cast<uint32_t>(PROT_READ | PROT_WRITE));
  bpf.Allow(SYS_mmap, {
                          {0, BPF_JEQ, 0},  // |addr| must be nullptr.
                          {kNot, BPF_JGT,
                           2 * 1024 * 1024 * 1024ul},  // no ridiculous lengths.
                          {kNot, BPF_JSET, kProtNotRW},  // No PROT_EXEC.
                      });
  bpf.Allow(SYS_madvise);    // Allocator and BufferedFrameDeserializer.
  bpf.Allow(SYS_read);       // General I/O.
  bpf.Allow(SYS_write);      // General I/O.
  bpf.Allow(SYS_readv);      // General I/O.
  bpf.Allow(SYS_writev);     // General I/O.
  bpf.Allow(SYS_lseek);      // General I/O.
  bpf.Allow(SYS_stat);       // General I/O.
  bpf.Allow(SYS_fstat);      // General I/O.
  bpf.Allow(SYS_lstat);      // General I/O.
  bpf.Allow(SYS_close);      // General I/O.
  bpf.Allow(SYS_ftruncate);  // General I/O.

// These are only available on 32-bit archs where sizeof(int) == 4.
#if defined(SYS_stat64) && SYS_stat64
  bpf.Allow(SYS_stat64);       // General I/O.
  bpf.Allow(SYS_fstat64);      // General I/O.
  bpf.Allow(SYS_lstat64);      // General I/O.
  bpf.Allow(SYS_ftruncate64);  // General I/O.
#endif

  bpf.Allow(SYS_nanosleep);        // Backpressure in ShaerdMemoryArbiter.
  bpf.Allow(SYS_clock_nanosleep);  // Libc wrappers might use this instead.
  bpf.Allow(SYS_socket);           // UnixSocket.
  bpf.Allow(SYS_accept);           // UnixSocket.
  bpf.Allow(SYS_accept4);          // UnixSocket.
  bpf.Allow(SYS_sendmsg);          // UnixSocket.
  bpf.Allow(SYS_recvmsg);          // UnixSocket.
  bpf.Allow(SYS_shutdown);         // UnixSocket.
  bpf.Allow(SYS_bind);             // UnixSocket.
  bpf.Allow(SYS_listen);           // UnixSocket.
  bpf.Allow(SYS_getsockname);      // UnixSocket.
  bpf.Allow(SYS_setsockopt);       // UnixSocket.
  bpf.Allow(SYS_getsockopt);       // UnixSocket.
  // Deliberately no connect() in the service.

  bpf.Allow(SYS_getpid);   // Android liblog.
  bpf.Allow(SYS_getuid);   // Android liblog.
  bpf.Allow(SYS_geteuid);  // Android liblog.
  bpf.Allow(SYS_getgid);   // Android liblog.
  bpf.Allow(SYS_gettid);   // Android liblog.
#if defined(SYS_getuid32) && SYS_getuid32
  bpf.Allow(SYS_getuid32);   // Android liblog.
  bpf.Allow(SYS_geteuid32);  // Android liblog.
  bpf.Allow(SYS_getgid32);   // Android liblog.
#endif
  bpf.Allow(SYS_futex);  // Android libc.so an libunwind use this.
  bpf.Allow(SYS_alarm);  // Some libc wrappers use this under the hoods.
  bpf.Allow(SYS_exit);

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

  // Allow mprotect, but only PROT_READ|PROT_WRITE, no PROT_EXEC or others.
  bpf.Allow(SYS_mprotect, {
                              {0, BPF_JGT, 0},  // |addr| must be > 0
                              {},
                              {kNot, BPF_JSET, kProtNotRW},  // No PROT_EXEC
                          });
  // Used by allocator. Don't allow MREMAP_FIXED though.
  bpf.Allow(SYS_mremap, {
                            {0, BPF_JGT, 0},  // |addr| must be > 0
                            {},
                            {},
                            {kNot, BPF_JSET, MREMAP_FIXED},
                        });
  bpf.Allow(SYS_munmap);
  bpf.Allow(SYS_kill, {{0, BPF_JEQ, 0}});  // Only self-signals.
#if BUILDFLAG(OS_ANDROID)
  bpf.Allow(__NR_memfd_create);
#endif

  // TODO: copy_file_range and/or sendfile

  bpf.EnterSandbox();
#endif
}

}  // namespace

int ServiceMain(int argc, char** argv) {
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

  InitServiceSandboxIfSupported();

  task_runner.Run();
  return 0;
}

}  // namespace perfetto
