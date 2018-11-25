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

class PagePool {
 public:
  class PageBlock {
   public:
    static constexpr size_t kPagesPerBlock = 32;  // 32 * 4KB = 128 KB.
    static constexpr size_t kMemSize = kPagesPerBlock * base::kPageSize;

    static PageBlock Create() { return PageBlock(); }

    ~PageBlock() {
      if (mem_.IsValid())
        PERFETTO_ELOG("--------block");  // DNS
    }

    PageBlock(PageBlock&&) noexcept = default;
    PageBlock& operator=(PageBlock&&) = default;

    bool is_full() const { return size_ >= kPagesPerBlock; }
    size_t size() const { return size_; }

    uint8_t* at(size_t i) const {
      PERFETTO_DCHECK(i < kPagesPerBlock);
      return reinterpret_cast<uint8_t*>(mem_.Get()) + i * base::kPageSize;
    }

   private:
    friend class PagePool;
    PageBlock(const PageBlock&) = delete;
    PageBlock& operator=(const PageBlock&) = delete;

    PageBlock() {
      PERFETTO_ILOG("++++++++block");  // DNS
      mem_ = base::PagedMemory::Allocate(kMemSize);
      static_assert(
          kPagesPerBlock <= std::numeric_limits<decltype(size_)>::max(),
          "size_ type is too small");
    }

    uint8_t* Allocate() {
      PERFETTO_CHECK(!is_full());
      return at(size_++);
    }

    void FreeLastPage(uint8_t* page) {
      PERFETTO_DCHECK(size_ > 0);
      size_--;
      PERFETTO_DCHECK(page == at(size_));
    }

    void Clear() {
      size_ = 0;
      mem_.AdviseDontNeed(mem_.Get(), kMemSize);
    }

    base::PagedMemory mem_;
    uint16_t size_ = 0;
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
