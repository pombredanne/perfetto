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

#ifndef SRC_TRACED_PROBES_FTRACE_PAGE_POOL_H_
#define SRC_TRACED_PROBES_FTRACE_PAGE_POOL_H_

#include <stdint.h>

#include <array>
#include <bitset>
#include <deque>
#include <limits>
#include <mutex>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/optional.h"
#include "perfetto/base/paged_memory.h"
#include "perfetto/base/thread_checker.h"
#include "perfetto/base/utils.h"

namespace perfetto {

// This class is a page allocator designed around the way the ftrace CpuReader
// (the thing that reads the kernel trace_pipe_raw) needs to manage memory.
// For context, CpuReader (and hence this class) is used on two threads:
// (1) A worker thread that writes into the buffer and (2) the main thread which
// reads all the content in big batches and turn them into protos.
// There is always at most one thread writing and one thread reading. In rare
// circumstances they can be active at the same time.
// It is optimized for the following use case:
// - Most of the times CpuReader wants to write 4096 bytes. In some rare cases
//   (read() during flush) it wants to write < 4096 bytes.
// - Even when it writes < 4096 bytes, CpuReader can figure out the size of the
//   payload from the ftrace header. We don't need extra tracking to tell how
//   much of each 4KB are used.
// - Doing a syscall for each page seems overkill. In most occasions CpuReader
//   does bursts of several pages in one go.
// - We can't really predict upfront how big the write bursts will be, hence we
//   cannot predict the size of the pool, unless we accept a very high bound.
//   In extreme conditions CpuReader will read the whole per-cpu ftrace buffer
//   in one go, while the reader is still reading the previous batch.
// - Write burst should not be too frequent, so once they are over it's worth
//   spending some extra cycles to release the memory.
// - The reader side always wants to read *all* the written pages in one batch.
//   While this happens though, the write might want to write more.
//
// The architecture of this class is as follows. Pages are organized in
// PageBlock(s). A PageBlock is simply an array of pages and is the elementary
// unit of memory allocation and frees. Pages within one block are cheaply
// allocated with a simple bump-pointer allocator.
// At any point a whole PageBlock can be in any of these tree lists:
//
// |allocated_|: PageBlock(s) allocated and being written by the worker thread.
//               This list is accessed only on the writer thread.
// |ready_|:     PageBlock(s) that have been completely written and are ready
//               be consumed by the reader.
//               This list is accessed by both threads holding the mutex.
// |freelist|:   PageBlock(s) that have been read and can be reused by the
//               writer.
//               This list is accessed by both threads holding the mutex.
class PagePool {
 public:

  class PageBlock {
   public:
    static constexpr size_t kPagesPerBlock = 32;  // 32 * 4KB = 128 KB.
    static constexpr size_t kBlockSize = kPagesPerBlock * base::kPageSize;

    // This factory method is just that we accidentally create extra blocks
    // without realizing by triggering the default constructor in containers.
    static PageBlock Create() { return PageBlock(); }

    PageBlock(PageBlock&&) noexcept = default;
    PageBlock& operator=(PageBlock&&) = default;

    bool is_full() const { return size_ >= kPagesPerBlock; }
    size_t size() const { return size_; }

    // Returns the pointer to the contents of the i-th page in the block.
    uint8_t* at(size_t i) const {
      PERFETTO_DCHECK(i < kPagesPerBlock);
      return reinterpret_cast<uint8_t*>(mem_.Get()) + i * base::kPageSize;
    }

    // Gets a new page. The caller (PagePool) needs to check upfront that the
    // pool is not full.
    uint8_t* Allocate() {
      PERFETTO_CHECK(!is_full());
      return at(size_++);
    }

    // Puts back the last page allocated. |page| is used only for DCHECKs.
    void FreeLastPage(uint8_t* page) {
      PERFETTO_DCHECK(size_ > 0);
      size_--;
      PERFETTO_DCHECK(page == at(size_));
    }

    // Releases memory of the block and marks it available for reuse.
    void Clear() {
      size_ = 0;
      mem_.AdviseDontNeed(mem_.Get(), kBlockSize);
    }

   private:
    PageBlock(const PageBlock&) = delete;
    PageBlock& operator=(const PageBlock&) = delete;
    PageBlock() { mem_ = base::PagedMemory::Allocate(kBlockSize); }

    base::PagedMemory mem_;
    size_t size_ = 0;
  };

  PagePool() {
    PERFETTO_DETACH_FROM_THREAD(writer_thread_);
    PERFETTO_DETACH_FROM_THREAD(reader_thread_);
  }

  uint8_t* Allocate() {
    PERFETTO_DCHECK_THREAD(writer_thread_);
    if (allocated_.empty() || allocated_.back().is_full()) {
      // Need a new PageBlock, try taking from the freelist or create a new one.
      std::lock_guard<std::mutex> lock(mutex_);
      if (freelist_.empty()) {
        allocated_.emplace_back(PageBlock::Create());
      } else {
        allocated_.emplace_back(std::move(freelist_.back()));
        freelist_.pop_back();
      }
      PERFETTO_DCHECK(allocated_.back().size() == 0);
    }
    return allocated_.back().Allocate();
  }

  void FreeLastPage(uint8_t* page) {
    PERFETTO_DCHECK_THREAD(writer_thread_);
    PERFETTO_DCHECK(!allocated_.empty());
    allocated_.back().FreeLastPage(page);
  }

  void FinishWrite() {
    PERFETTO_DCHECK_THREAD(writer_thread_);
    std::lock_guard<std::mutex> lock(mutex_);
    ready_.insert(ready_.end(), std::make_move_iterator(allocated_.begin()),
                  std::make_move_iterator(allocated_.end()));
    allocated_.clear();
  }

  std::vector<PageBlock> BeginRead() {
    PERFETTO_DCHECK_THREAD(reader_thread_);
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(ready_);
  }

  void EndRead(std::vector<PageBlock> page_blocks) {
    PERFETTO_DCHECK_THREAD(reader_thread_);
    for (PageBlock& page_block : page_blocks)
      page_block.Clear();

    std::lock_guard<std::mutex> lock(mutex_);
    freelist_.insert(freelist_.end(),
                     std::make_move_iterator(page_blocks.begin()),
                     std::make_move_iterator(page_blocks.end()));
  }

  PERFETTO_THREAD_CHECKER(writer_thread_)
  std::vector<PageBlock> allocated_;  // Accessed exclusively by the writer.

  std::mutex mutex_;  // Protects all fields below.

  PERFETTO_THREAD_CHECKER(reader_thread_)
  std::vector<PageBlock> ready_;     // Accessed by both threads.
  std::vector<PageBlock> freelist_;  // Accessed by both threads.
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FTRACE_PAGE_POOL_H_
