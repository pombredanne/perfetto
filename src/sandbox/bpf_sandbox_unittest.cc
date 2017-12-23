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

#define ASSERT_SANDBOX_TRAP(x)    \
  ASSERT_EXIT(                    \
      {                           \
        bpf->EnterSandboxOrDie(); \
        x                         \
      },                          \
      ::testing::KilledBySignal(SIGSYS), "")

#define ASSERT_SANDBOX_ALLOW(x)   \
  ASSERT_EXIT(                    \
      {                           \
        bpf->EnterSandboxOrDie(); \
        x _exit(0);               \
      },                          \
      ::testing::ExitedWithCode(0), "");

namespace perfetto {
namespace {

constexpr auto kNot = BpfSandbox::kNot;

class BpfSandboxTest : public ::testing::Test {
 public:
  void SetUp() override {
    bpf.reset(new BpfSandbox());
    bpf->Allow(SYS_exit_group);
  }
  void TearDown() override { bpf.reset(); }
  std::unique_ptr<BpfSandbox> bpf;
};

TEST_F(BpfSandboxTest, SimplePolicy) {
  bpf->Allow(SYS_write);
  ASSERT_SANDBOX_ALLOW({ write(STDOUT_FILENO, "\n", 1); });
  ASSERT_SANDBOX_TRAP({ base::ignore_result(fork()); });
}

TEST_F(BpfSandboxTest, SyscallArgumentFilter) {
  bpf->Allow(SYS_write);
#if defined(SYS_mmap)
#define MMAP_SYSCALL SYS_mmap
#else
#define MMAP_SYSCALL SYS_mmap2
#endif
  bpf->Allow(
      MMAP_SYSCALL,
      {
          {0, BPF_JEQ, 0},  // |addr| must be nullptr
          {0, BPF_JGT, 0},  // |length| must be  > 0
          {kNot, BPF_JSET, static_cast<uint32_t>(~(PROT_READ | PROT_WRITE))},
          {},               // No filters on |flags|
          {0, BPF_JEQ, 0},  // |fd| must be == 0
      });
  base::ScopedFile devnull(open("/dev/null", O_RDONLY));
  bpf->Allow(SYS_read, {{0, BPF_JEQ, static_cast<uint32_t>(*devnull)}});

  ASSERT_SANDBOX_ALLOW(
      { mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0); });

  // Should trap because of nonzero address.
  ASSERT_SANDBOX_TRAP({
    void* nonzero_addr = bpf.get();
    mmap(nonzero_addr, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
  });

  // Should trap because of zero length.
  ASSERT_SANDBOX_TRAP(
      { mmap(0, 0, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0); });

  // Should trap because of PROT_EXEC.
  ASSERT_SANDBOX_TRAP(
      { mmap(0, 4096, PROT_READ | PROT_EXEC, MAP_ANONYMOUS, 0, 0); });

  // Should trap because of extra PROT_ flag.
  ASSERT_SANDBOX_TRAP(
      { mmap(0, 4096, PROT_READ | PROT_WRITE | 0x80, MAP_ANONYMOUS, 0, 0); });

  // Should trap because of nonzero FD arg.
  ASSERT_SANDBOX_TRAP(
      { mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 42, 0); });

  // Should trap because of the combination of the above.
  ASSERT_SANDBOX_TRAP(
      { mmap(bpf.get(), 0, PROT_READ | PROT_EXEC, MAP_ANONYMOUS, 42, 0); });

  ASSERT_SANDBOX_ALLOW({
    char c;
    base::ignore_result(read(*devnull, &c, 1));
  });

  // Should trap because of FD != |devnull|.
  ASSERT_SANDBOX_TRAP({
    char c;
    base::ignore_result(read(STDIN_FILENO, &c, 1));
  });
}

// Tests that when applying several filters to the same syscall number, those
// filter have OR semantic.
TEST_F(BpfSandboxTest, ArgFiltersHaveORSemantic) {
  bpf->Allow(SYS_write, {
                            {0, BPF_JEQ, static_cast<uint32_t>(STDOUT_FILENO)},
                        });
  bpf->Allow(SYS_write, {
                            {0, BPF_JEQ, static_cast<uint32_t>(STDERR_FILENO)},
                        });

  ASSERT_SANDBOX_ALLOW({
    write(STDOUT_FILENO, "\n", 1);
    write(STDERR_FILENO, "\n", 1);
  });

  ASSERT_SANDBOX_TRAP({ write(42, "\n", 1); });
}

}  // namespace
}  // namespace perfetto
