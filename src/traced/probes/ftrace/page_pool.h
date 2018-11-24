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

#include <bitset>
#include <deque>
#include <mutex>

#include "perfetto/base/logging.h"
#include "perfetto/base/optional.h"
#include "perfetto/base/paged_memory.h"
#include "perfetto/base/utils.h"

namespace perfetto {

class PagePool {
 public:
  class Page {
   public:
    explicit Page(uint8_t* data) : data_(data) {}
    Page(Page&&) noexcept = default;
    Page& operator=(Page&&) = default;

    constexpr size_t size() const { return base::kPageSize; }

    void set_used_size(uint32_t used_size) {
      PERFETTO_DCHECK(used_size <= base::kPageSize);
      used_size_ = used_size;
    }
    uint32_t used_size() const { return used_size_; }
    uint8_t* data() const { return data_; }

   private:
    uint8_t* data_;
    uint32_t used_size_ = 0;
  };

  explicit PagePool(uint32_t num_pages);
  base::Optional<Page> GetFreePage();
  void PushContentfulPage(Page, ssize_t used_size);
  base::Optional<Page> PopContentfulPage();
  void FreePage(Page);

 private:
  size_t IndexOf(const Page& page);
  uint8_t* mem() { return reinterpret_cast<uint8_t*>(mem_.Get()); }

  static constexpr uint32_t kMaxPages = 64;
  const uint32_t num_pages_;
  base::PagedMemory mem_;

  std::mutex mutex_;  // Protects all fields below.
  std::bitset<kMaxPages> free_pages_;
  std::deque<Page> contentful_pages_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FTRACE_PAGE_POOL_H_
