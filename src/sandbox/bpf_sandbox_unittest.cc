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

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "gtest/gtest.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/utils.h"

void EnableStacktraceOnCrashForDebug();

namespace perfetto {
namespace {

constexpr auto kNot = BpfSandbox::kNot;

class BpfSandboxTest : public ::testing::Test {
 public:
  void SetUp() override {
    bpf.reset(new BpfSandbox(SECCOMP_RET_TRAP));
    bpf->AllowSyscall(SYS_exit_group);

    // Needed by debug_crash_stack_trace.cc in case of failure.
    bpf->AllowSyscall(SYS_futex);
    bpf->AllowSyscall(SYS_rt_sigaction);
  }
  void TearDown() override { bpf.reset(); }
  std::unique_ptr<BpfSandbox> bpf;
};

TEST_F(BpfSandboxTest, SimplePolicy) {
  bpf->AllowSyscall(SYS_write);
  ASSERT_EXIT(
      {
        bpf->EnterSandbox();
        write(STDOUT_FILENO, "\n", 1);
        _exit(7);
      },
      ::testing::ExitedWithCode(7), "");
  ASSERT_DEATH(
      {
        bpf->EnterSandbox();
        base::ignore_result(fork());
      },
      "");
}

TEST_F(BpfSandboxTest, SyscallArgumentFilter) {
  bpf->AllowSyscall(SYS_write);
  bpf->AllowSyscall(
      SYS_mmap,
      {
          {0, BPF_JEQ, 0},  // |addr| must be nullptr
          {0, BPF_JGT, 0},  // |length| must be  > 0
          {kNot, BPF_JSET, static_cast<uint32_t>(~(PROT_READ | PROT_WRITE))},
          {},               // No filters on |flags|
          {0, BPF_JEQ, 0},  // |fd| must be == 0
      });
  base::ScopedFile devnull(open("/dev/null", O_RDONLY));
  bpf->AllowSyscall(SYS_read, {{0, BPF_JEQ, static_cast<uint32_t>(*devnull)}});
  ASSERT_EXIT(
      {
        bpf->EnterSandbox();
        mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
        _exit(0);
      },
      ::testing::ExitedWithCode(0), "");

  ASSERT_DEATH(  // Should trap because of nonzero address.
      {
        bpf->EnterSandbox();
        void* nonzero_addr = bpf.get();
        mmap(nonzero_addr, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
      },
      "");

  ASSERT_DEATH(  // Should trap because of zero length.
      {
        bpf->EnterSandbox();
        mmap(0, 0, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
      },
      "");

  ASSERT_DEATH(  // Should trap because of PROT_EXEC.
      {
        bpf->EnterSandbox();
        mmap(0, 4096, PROT_READ | PROT_EXEC, MAP_ANONYMOUS, 0, 0);
      },
      "");

  ASSERT_DEATH(  // Should trap because of PROT_SEM.
      {
        bpf->EnterSandbox();
        mmap(0, 4096, PROT_READ | PROT_SEM, MAP_ANONYMOUS, 0, 0);
      },
      "");

  ASSERT_DEATH(  // Should trap because of nonzero FD arg.
      {
        bpf->EnterSandbox();
        mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 42, 0);
      },
      "");

  ASSERT_DEATH(  // Should trap because of the combination of the above.
      {
        bpf->EnterSandbox();
        mmap(bpf.get(), 0, PROT_READ | PROT_EXEC, MAP_ANONYMOUS, 42, 0);
      },
      "");

  ASSERT_EXIT(
      {
        bpf->EnterSandbox();
        char c;
        base::ignore_result(read(*devnull, &c, 1));
        _exit(0);
      },
      ::testing::ExitedWithCode(0), "");

  ASSERT_DEATH(  // Should trap because of FD != |devnull|.
      {
        bpf->EnterSandbox();
        char c;
        base::ignore_result(read(STDIN_FILENO, &c, 1));
      },
      "");
}

// Tests that when applying several filters to the same syscall number, those
// filter have AND semantic.
TEST_F(BpfSandboxTest, ArgFiltersHaveANDSemantic) {
  // The resulting filter should be the intersection of the three, i.e.:
  // SYS_mmap is only allowed if |addr| == nullptr AND 0 < |length| < 8192
  bpf->AllowSyscall(SYS_mmap, {
                                  {0, BPF_JEQ, 0},  // |addr| must be nullptr
                              });
  bpf->AllowSyscall(SYS_mmap, {
                                  {},               // |addr| must be nullptr
                                  {0, BPF_JGT, 0},  // |length| must be  > 0
                              });
  bpf->AllowSyscall(SYS_mmap,
                    {
                        {},                     // |addr| must be nullptr
                        {kNot, BPF_JGE, 8192},  // |length| must be  < 8192
                    });
  ASSERT_EXIT(
      {
        bpf->EnterSandbox();
        mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
        _exit(0);
      },
      ::testing::ExitedWithCode(0), "");

  ASSERT_DEATH(
      {
        bpf->EnterSandbox();
        void* nonzero_addr = bpf.get();
        mmap(nonzero_addr, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
      },
      "");

  ASSERT_DEATH(
      {
        bpf->EnterSandbox();
        mmap(0, 0, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
      },
      "");

  ASSERT_DEATH(
      {
        bpf->EnterSandbox();
        mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
      },
      "");
}

}  // namespace
}  // namespace perfetto
