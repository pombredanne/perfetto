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

#include "src/sandbox/bpf_sandbox.h"

#include <linux/seccomp.h>
#include <sys/syscall.h>

#include "gtest/gtest.h"
#include "perfetto/base/logging.h"

namespace perfetto {
namespace {

TEST(BpfSandboxTest, Simple) {
  BPFSandbox bpf(SECCOMP_RET_TRAP);
  bpf.AllowSyscall(SYS_ppoll);  // For task runners.
  bpf.AllowSyscall(SYS_read);
  bpf.AllowSyscall(SYS_madvise);
  bpf.AllowSyscall(SYS_write);
  bpf.AllowSyscall(SYS_mmap);
  bpf.AllowSyscall(SYS_munmap);
  bpf.AllowSyscall(SYS_mprotect);
  bpf.AllowSyscall(SYS_futex);         // For liunwind.
  bpf.AllowSyscall(SYS_rt_sigaction);  // Only debug.
  bpf.AllowSyscall(SYS_exit_group);
  bpf.EnterSandbox();
}
}  // namespace
}  // namespace perfetto
