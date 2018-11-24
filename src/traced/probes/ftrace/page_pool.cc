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

#include "src/traced/probes/ftrace/page_pool.h"

namespace perfetto {

PagePool::PagePool(uint32_t num_pages) : num_pages_(num_pages) {
  PERFETTO_CHECK(num_pages <= kMaxPages);
  mem_ = base::PagedMemory::Allocate(num_pages * base::kPageSize);
  for (uint32_t i = 0; i < num_pages; i++)
    free_pages_.set(i);
}

size_t PagePool::IndexOf(const Page& page) {
  PERFETTO_DCHECK(page.data() >= mem() &&
                  page.data() <= mem() + (num_pages_ - 1) * base::kPageSize);
  auto off = static_cast<size_t>(page.data() - mem());
  PERFETTO_DCHECK(off % base::kPageSize == 0);
  return static_cast<size_t>(off / base::kPageSize);
}

base::Optional<PagePool::Page> PagePool::GetFreePage() {
  std::lock_guard<std::mutex> lock(mutex_);
  uint32_t p;
  // TODO(primiano): optimize this.
  for (p = 0; p <= num_pages_; p++) {
    if (free_pages_.test(p))
      break;
  }
  if (p >= num_pages_)
    return base::nullopt;
  free_pages_.set(p, false);
  return Page(mem() + p * base::kPageSize);
}

void PagePool::FreePage(Page page) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t page_index = IndexOf(page);
  PERFETTO_DCHECK(page_index < num_pages_);
  PERFETTO_DCHECK(!free_pages_.test(page_index));
  free_pages_.set(page_index);
}

void PagePool::PushContentfulPage(Page page, ssize_t used_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t page_index = IndexOf(page);
  PERFETTO_DCHECK(page_index < num_pages_);
  PERFETTO_DCHECK(!free_pages_.test(page_index));
  page.set_used_size(static_cast<uint32_t>(used_size));
  contentful_pages_.push_back(std::move(page));
}

base::Optional<PagePool::Page> PagePool::PopContentfulPage() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (contentful_pages_.empty())
    return base::nullopt;
  Page page = std::move(contentful_pages_.front());
  contentful_pages_.pop_front();
  PERFETTO_DCHECK(IndexOf(page) < num_pages_);
  PERFETTO_DCHECK(!free_pages_.test(IndexOf(page)));
  return page;
}

}  // namespace perfetto
