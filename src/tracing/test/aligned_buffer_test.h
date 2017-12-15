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

#ifndef SRC_TRACING_TEST_ALIGNED_BUFFER_TEST_H_
#define SRC_TRACING_TEST_ALIGNED_BUFFER_TEST_H_

#include <stdlib.h>

#include <memory>

#include "gtest/gtest.h"
#include "perfetto/base/utils.h"

namespace perfetto {

// Base parametrized test for unittests that require an aligned buffer.
class AlignedBufferTest : public ::testing::TestWithParam<size_t> {
 public:
  static constexpr size_t kNumPages = 14;
  void SetUp() override;
  void TearDown() override;

  size_t buf_size() const { return buf_size_; }
  size_t page_size() const { return page_size_; }
  uint8_t* buf() const { return buf_.get(); }

 private:
  size_t buf_size_ = 0;
  size_t page_size_ = 0;
  std::unique_ptr<uint8_t, base::FreeDeleter> buf_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_TEST_ALIGNED_BUFFER_TEST_H_
