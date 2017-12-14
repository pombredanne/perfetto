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

#include "src/tracing/test/aligned_buffer_test.h"

#include "perfetto/base/logging.h"

namespace perfetto {

// static
constexpr size_t AlignedBufferTest::kNumPages;

void AlignedBufferTest::SetUp() {
  page_size_ = GetParam();
  buf_size_ = page_size_ * kNumPages;
  void* mem = nullptr;
  PERFETTO_CHECK(posix_memalign(&mem, page_size_, buf_size_) == 0);
  buf_.reset(reinterpret_cast<uint8_t*>(mem));
  memset(buf_.get(), 0, buf_size_);
}

void AlignedBufferTest::TearDown() {
  buf_.reset();
}

}  // namespace perfetto
