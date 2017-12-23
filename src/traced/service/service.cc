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
  bpf.AllowSyscall(SYS_ppoll);  // For task runners.
  bpf.AllowSyscall(SYS_poll);   // For task runners.
  const uint32_t kProtRW = static_cast<uint32_t>(~(PROT_READ | PROT_WRITE));
  bpf.AllowSyscall(
      SYS_mmap,
      {
          {0, BPF_JEQ, 0},                            // |addr| must be nullptr
          {kNot, BPF_JGT, 2 * 1024 * 1024 * 1024ul},  // no ridiculous lengths
          {kNot, BPF_JSET, kProtRW},                  // No PROT_EXEC
      });
  bpf.AllowSyscall(SYS_madvise);
  bpf.AllowSyscall(SYS_read);
  bpf.AllowSyscall(SYS_write);
  bpf.AllowSyscall(SYS_writev);
  bpf.AllowSyscall(SYS_readv);
  bpf.AllowSyscall(SYS_lseek);
  bpf.AllowSyscall(SYS_fstat);
  bpf.AllowSyscall(SYS_brk);
  bpf.AllowSyscall(SYS_nanosleep);
  bpf.AllowSyscall(SYS_socket);
  bpf.AllowSyscall(SYS_accept);
  bpf.AllowSyscall(SYS_accept4);
  bpf.AllowSyscall(SYS_sendmsg);
  bpf.AllowSyscall(SYS_recvmsg);
  bpf.AllowSyscall(SYS_getuid);
  bpf.AllowSyscall(SYS_shutdown);
  bpf.AllowSyscall(SYS_close);
  bpf.AllowSyscall(SYS_bind);
  bpf.AllowSyscall(SYS_listen);
  bpf.AllowSyscall(SYS_getsockname);
  bpf.AllowSyscall(SYS_setsockopt);
  bpf.AllowSyscall(SYS_getsockopt);
  bpf.AllowSyscall(SYS_exit);
  bpf.AllowSyscall(SYS_fcntl);
  bpf.AllowSyscall(SYS_ftruncate);
  bpf.AllowSyscall(SYS_gettimeofday);
  bpf.AllowSyscall(SYS_getrusage);
  bpf.AllowSyscall(SYS_mprotect, {
                                     {0, BPF_JGT, 0},  // |addr| must be > 0
                                     {},
                                     {kNot, BPF_JSET, kProtRW},  // No PROT_EXEC
                                 });
  bpf.AllowSyscall(SYS_mremap, {
                                   {0, BPF_JGT, 0},  // |addr| must be > 0
                                   {},
                                   {},
                                   {kNot, BPF_JSET, MREMAP_FIXED},
                               });
  bpf.AllowSyscall(SYS_munmap);
  bpf.AllowSyscall(SYS_kill, {{0, BPF_JEQ, 0}});  // Only self-signals.
#if BUILDFLAG(OS_ANDROID)
  bpf.AllowSyscall(__NR_memfd_create);
#endif

  // TODO: copy_file_range and/or sendfile

  bpf.EnableBaselinePolicy();
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
