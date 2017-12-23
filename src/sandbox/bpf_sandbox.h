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
  struct ArgMatcher {
    uint16_t flags;  // Either 0 or kNot, which inverts |predicate|.
    uint16_t pred;   // BPF_JEQ, BPF_JGT, BPF_JGE, BPF_JSET.
    uint32_t value;  // Immediate value.
  };

  BpfSandbox(uint32_t fail_action);

  // Unconditionally whitelist the given syscall (SYS_xxx).
  void Allow(unsigned int nr);

  // Filters each argument against a matcher. Each matcher is of the form:
  // {BPF_JEQ, N}, {BPF_JGT, N}, {BPF_JSET, N}.
  // If multiple filters are applied to the same syscall number, those have
  // OR semantic.
  void Allow(unsigned int nr, std::initializer_list<ArgMatcher> args);

  // Crashes with a CHECK() in case of any error.
  void EnterSandbox();

 private:
  void append(struct sock_filter value) {
    PERFETTO_DCHECK(prog_size_ < kProgSize);
    prog_[prog_size_++] = value;
  }

  static constexpr size_t kProgSize = 256;
  const uint32_t fail_action_;
  bool finalized_ = false;

  struct sock_filter prog_[kProgSize];
  size_t prog_size_ = 0;
};

}  // namespace perfetto

#endif  // SRC_SANDBOX_BPF_SANDBOX_H_
