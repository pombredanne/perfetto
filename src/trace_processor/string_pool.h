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

#ifndef SRC_TRACE_PROCESSOR_STRING_POOL_H_
#define SRC_TRACE_PROCESSOR_STRING_POOL_H_

#include <string.h>

#include <array>
#include <memory>
#include <unordered_map>

#include "perfetto/base/logging.h"
#include "perfetto/base/string_view.h"

namespace perfetto {
namespace trace_processor {

// StringId is an offset into |string_pool_|.
using StringId = uint32_t;

// An append-only pool of string that efficiently handles indexing and
// deduplication. Note that an empty pool has a non-negligible cost of ~96KB.
class StringPool {
 public:
  StringPool();

  StringId Insert(base::StringView);

  base::StringView Get(StringId string_id) const {
    size_t block_id = string_id >> kOffsetBits;
    size_t offset = string_id & kMaxOffset;
    PERFETTO_CHECK(block_id < num_blocks_);
    const Block& block = *blocks_[block_id];

    PERFETTO_CHECK(offset < sizeof(block.data) - sizeof(Length) - 1);
    Length len;
    const char* data = &block.data[offset];
    memcpy(&len, data, sizeof(Length));
    data += sizeof(Length);
    PERFETTO_CHECK(data + len < &block.data[sizeof(block.data)]);
    return base::StringView(data, len);
  }

  size_t size() const { return num_strings_; }

 private:
  // Each string in the pool stored in |data| is prefixed with its len.
  using Length = uint16_t;
  static constexpr unsigned kNumBlocks = 4096;  // ~ 256 MB of strings.
  static constexpr unsigned kOffsetBits = 16;
  static constexpr unsigned kMaxOffset = (1ul << kOffsetBits) - 1;
  static constexpr unsigned kBlockIdBits = sizeof(StringId) * 8 - kOffsetBits;
  static constexpr unsigned kMaxBlockId = (1ul << kBlockIdBits) - 1;

  struct Block {
    Block() = default;
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;

    size_t space_left() const { return sizeof(data) - offset; }

    uint32_t offset = 0;  // Next unused char in |data|;
    char data[64 * 1024 - sizeof(offset)];
  };

  std::array<std::unique_ptr<Block>, kNumBlocks> blocks_;
  size_t num_blocks_ = 0;

  using StringHash = uint64_t;
  std::unordered_map<StringHash, StringId> index_;
  size_t num_strings_ = 0;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_STRING_POOL_H_
