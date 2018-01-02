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

#ifndef SRC_SANDBOX_BPF_SANDBOX_H_
#define SRC_SANDBOX_BPF_SANDBOX_H_

#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stdint.h>

#include <initializer_list>

#include "perfetto/base/logging.h"

namespace perfetto {

class BpfSandbox {
 public:
  static constexpr uint16_t kNot = 1;
  static constexpr size_t kMaxArgs = 6;

  // Syscall argument filter. Can be used to filter the individual syscall
  // arguments. The empty initializer {} causes the given argument to be
  // unconditionally allowed (because |op| == 0 == BPF_JA -> Jump Always).
  struct ArgFilter {
    uint16_t flags;  // Either 0 or kNot, which inverts |op|.
    uint16_t op;     // BPF_JEQ, BPF_JGT, BPF_JGE, BPF_JSET.
    uint32_t value;  // Immediate value.

    bool empty() const { return !flags && !op && !value; }
  };

  // Syscall filter. |args| has AND semantic, i.e. the SyscallFilter allows
  // the syscall only if all ArgFilters are satisfied.
  struct SyscallFilter {
    unsigned int nr;           // Syscall number, from syscall.h.
    ArgFilter args[kMaxArgs];  // Optional per-argument filter.

    // Returns 1 + the index of the last non-null |args| entry, or 0 if all
    // |args| are null.
    size_t num_args() const;
  };

  BpfSandbox();

  // Adds an array of syscalls to the whitelist. Can be called multiple times
  // and has additive semantic (i.e. Allow(0..9) == Allow(0..4) + Allow(5..9)).
  // If multiple filters are applied to the same syscall number, those have
  // OR semantic (See ArgFiltersHaveORSemantic in bpf_sandbox_unittest.cc).
  void Allow(const SyscallFilter filters[], size_t filters_size);

  // Syntactic sugar that allows to use the syntax: Allow({{SYS_x, {}}, ... }).
  template <size_t N>
  void Allow(const SyscallFilter (&filters)[N]) {
    Allow(filters, N);
  }

  // Crashes with a CHECK() in case of any error.
  void EnterSandboxOrDie();

 private:
  void AllowOne(const SyscallFilter&);

  void Append(struct sock_filter value) {
    PERFETTO_DCHECK(prog_size_ < kProgSize);
    prog_[prog_size_++] = value;
  }

  static constexpr size_t kProgSize = 256;
  bool finalized_ = false;

  struct sock_filter prog_[kProgSize];
  size_t prog_size_ = 0;
};

}  // namespace perfetto

#endif  // SRC_SANDBOX_BPF_SANDBOX_H_
