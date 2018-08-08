/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <unwindstack/Memory.h>
#include <unwindstack/Regs.h>
#include <unwindstack/RegsArm.h>
#include <unwindstack/RegsArm64.h>
#include <unwindstack/RegsMips.h>
#include <unwindstack/RegsMips64.h>
#include <unwindstack/RegsX86.h>
#include <unwindstack/RegsX86_64.h>
#include <unwindstack/Unwinder.h>
#include <unwindstack/UserArm.h>
#include <unwindstack/UserArm64.h>
#include <unwindstack/UserMips.h>
#include <unwindstack/UserMips64.h>
#include <unwindstack/UserX86.h>
#include <unwindstack/UserX86_64.h>

#include "perfetto/base/logging.h"
#include "src/profiling/memory/transport_data.h"
#include "src/profiling/memory/unwinding.h"

namespace perfetto {

namespace {

size_t kMaxFrames = 1000;

class StackMemory : public unwindstack::Memory {
 public:
  StackMemory(int mem_fd, uint64_t sp, uint8_t* stack, size_t size)
      : mem_fd_(mem_fd),
        sp_(sp),
        stack_end_(sp + size),
        stack_(stack),
        size_(size) {}

  size_t Read(uint64_t addr, void* dst, size_t size) override {
    if (addr >= sp_ && addr + size_ < stack_end_) {
      size_t offset = addr - sp_;
      memcpy(dst, stack_ + offset, size);
      return size;
    }

    if (lseek(mem_fd_, static_cast<off_t>(addr), SEEK_SET) == -1)
      return 0;

    ssize_t rd = read(mem_fd_, &dst, size);
    if (rd == -1)
      return 0;
    return static_cast<size_t>(rd);
  }

 private:
  int mem_fd_;
  uint64_t sp_;
  uint64_t stack_end_;
  uint8_t* stack_;
  size_t size_;
};

unwindstack::Regs* CreateFromRawData(unwindstack::ArchEnum arch,
                                     void* raw_data) {
  switch (arch) {
    case unwindstack::ARCH_X86:
      return unwindstack::RegsX86::Read(raw_data);
    case unwindstack::ARCH_X86_64:
      return unwindstack::RegsX86_64::Read(raw_data);
    case unwindstack::ARCH_ARM:
      return unwindstack::RegsArm::Read(raw_data);
    case unwindstack::ARCH_ARM64:
      return unwindstack::RegsArm64::Read(raw_data);
    case unwindstack::ARCH_MIPS:
      return unwindstack::RegsMips::Read(raw_data);
    case unwindstack::ARCH_MIPS64:
      return unwindstack::RegsMips64::Read(raw_data);
    case unwindstack::ARCH_UNKNOWN:
      return nullptr;
  }
}

}  // namespace

void DoUnwind(void* mem, size_t sz, ProcessMetadata* metadata) {
  if (sz < sizeof(AllocMetadata)) {
    PERFETTO_ELOG("size");
    return;
  }
  AllocMetadata* alloc_metadata = reinterpret_cast<AllocMetadata*>(mem);
  unwindstack::Regs* regs =
      CreateFromRawData(alloc_metadata->arch, alloc_metadata->reg_data);
  if (regs == nullptr) {
    PERFETTO_ELOG("regs");
    return;
  }
  if (alloc_metadata->stack_pointer_offset < sizeof(AllocMetadata) ||
      alloc_metadata->stack_pointer_offset > sz) {
    PERFETTO_ELOG("out-of-bound stack_pointer_offset");
    return;
  }
  uint8_t* stack =
      reinterpret_cast<uint8_t*>(mem) + alloc_metadata->stack_pointer_offset;
  std::shared_ptr<unwindstack::Memory> mems = std::make_shared<StackMemory>(
      metadata->pid, alloc_metadata->stack_pointer, stack,
      sz - alloc_metadata->stack_pointer_offset);
  unwindstack::Unwinder unwinder(kMaxFrames, &metadata->maps, regs, mems);
  int error_code;
  for (int attempt = 0; attempt < 2; ++attempt) {
    unwinder.Unwind();
    error_code = unwinder.LastErrorCode();
    if (error_code != 0) {
      if (error_code == unwindstack::ERROR_INVALID_MAP && attempt == 0) {
        metadata->maps = unwindstack::RemoteMaps(metadata->pid);
        metadata->maps.Parse();
      } else {
        break;
      }
    } else {
      break;
    }
  }
}

}  // namespace perfetto
