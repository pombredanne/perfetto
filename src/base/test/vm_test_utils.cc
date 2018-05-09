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

#include "src/base/test/vm_test_utils.h"

#include "perfetto/base/build_config.h"

#include <memory>

#include <errno.h>
#include <string.h>

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <Windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#endif

#include "gtest/gtest.h"
#include "perfetto/base/build_config.h"

namespace perfetto {
namespace base {
namespace vm_test_utils {

bool IsMapped(void* start, size_t size) {
  EXPECT_EQ(0u, size % 4096);
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  MEMORY_BASIC_INFORMATION memory_info = {};
  size_t res = VirtualQuery(start, &memory_info, size);
  EXPECT_EQ(res, sizeof(memory_info));
  EXPECT_EQ(memory_info.BaseAddress, start);
  if (!memory_info.AllocationBase)
    return false;
  // If RegionSize is smaller than size that means the range has varying
  // attributes, so a true/false answer is impossible.
  EXPECT_GE(memory_info.RegionSize, size);
  return memory_info.State == MEM_COMMIT;
#else
#if PERFETTO_BUILDFLAG(PERFETTO_OS_MACOSX)
  using PageState = char;
  static constexpr PageState kIncoreMask = MINCORE_INCORE;
#else
  using PageState = unsigned char;
  static constexpr PageState kIncoreMask = 1;
#endif
  const size_t num_pages = size / 4096;
  std::unique_ptr<PageState[]> page_states(new PageState[num_pages]);
  memset(page_states.get(), 0, num_pages * sizeof(PageState));
  int res = mincore(start, size, page_states.get());
  // Linux returns ENOMEM when an unmapped memory range is passed.
  // MacOS instead returns 0 but leaves the page_states empty.
  if (res == -1 && errno == ENOMEM)
    return false;
  EXPECT_EQ(0, res);
  for (size_t i = 0; i < num_pages; i++) {
    if (!(page_states[i] & kIncoreMask))
      return false;
  }
  return true;
#endif
}

}  // namespace vm_test_utils
}  // namespace base
}  // namespace perfetto
