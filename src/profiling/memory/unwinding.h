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

#ifndef SRC_PROFILING_MEMORY_UNWINDING_H_
#define SRC_PROFILING_MEMORY_UNWINDING_H_

#include <unwindstack/Maps.h>
#include <unwindstack/Unwinder.h>
#include "perfetto/base/scoped_file.h"
#include "src/profiling/memory/bookkeeping.h"
#include "src/profiling/memory/bounded_queue.h"
#include "src/profiling/memory/wire_protocol.h"

namespace perfetto {

// Read /proc/[pid]/maps from an open file descriptor.
// TODO(fmayer): Figure out deduplication to other maps.
class FileDescriptorMaps : public unwindstack::Maps {
 public:
  FileDescriptorMaps(base::ScopedFile fd);
  bool Parse() override;
  void Reset();

 private:
  base::ScopedFile fd_;
};

struct ProcessMetadata {
  ProcessMetadata(pid_t p, base::ScopedFile maps_fd, base::ScopedFile mem)
      : pid(p), maps(std::move(maps_fd)), mem_fd(std::move(mem)) {
    PERFETTO_CHECK(maps.Parse());
  }
  pid_t pid;
  FileDescriptorMaps maps;
  base::ScopedFile mem_fd;
};

// Overlays size bytes pointed to by stack for addresses in [sp, sp + size).
// Addresses outside of that range are read from mem_fd, which should be an fd
// that opened /proc/[pid]/mem.
class StackMemory : public unwindstack::Memory {
 public:
  StackMemory(int mem_fd, uint64_t sp, uint8_t* stack, size_t size);
  size_t Read(uint64_t addr, void* dst, size_t size) override;

 private:
  int mem_fd_;
  uint64_t sp_;
  uint64_t stack_end_;
  uint8_t* stack_;
};

size_t RegSize(unwindstack::ArchEnum arch);

struct UnwindingRecord {
  pid_t pid;
  size_t size;
  std::unique_ptr<uint8_t[]> data;
  std::weak_ptr<ProcessMetadata> metadata;
};

struct FreeRecord {
  std::unique_ptr<uint8_t[]> free_data;
  FreeMetadata* metadata;
};

struct AllocRecord {
  AllocMetadata alloc_metadata;
  std::vector<unwindstack::FrameData> frames;
};

enum class BookkeepingRecordType {
  Dump = 0,
  Malloc = 1,
  Free = 2,
};

struct BookkeepingRecord {
  uint64_t pid;
  // TODO(fmayer): Use a union.
  BookkeepingRecordType record_type;
  AllocRecord alloc_record;
  FreeRecord free_record;
};

bool DoUnwind(WireMessage*, ProcessMetadata* metadata, AllocRecord* out);

bool HandleUnwindingRecord(UnwindingRecord* rec, BookkeepingRecord* out);

void UnwindingMainLoop(BoundedQueue<UnwindingRecord>* input_queue,
                       BoundedQueue<BookkeepingRecord>* output_queue);

struct BookkeepingData {
  BookkeepingData(GlobalCallstackTrie* callsites) : heap_tracker(callsites) {}

  HeapTracker heap_tracker;
  uint64_t ref_count = 0;
};

class BookkeepingActor {
 public:
  BookkeepingActor(BoundedQueue<BookkeepingRecord>* input_queue,
                   GlobalCallstackTrie* callsites)
      : input_queue_(input_queue), callsites_(callsites) {}

  void Run();
  void AddSocket(uint64_t pid);
  void RemoveSocket(uint64_t pid);

 private:
  void HandleBookkeepingRecord(BookkeepingRecord* rec);

  BoundedQueue<BookkeepingRecord>* const input_queue_;
  GlobalCallstackTrie* const callsites_;

  std::map<uint64_t, BookkeepingData> bookkeeping_data_;
  std::mutex bookkeeping_mutex_;
};

}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_UNWINDING_H_
