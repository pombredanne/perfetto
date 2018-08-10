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

// StringId is actually just the actual pointer to the beginning of the string.
using StringId = uintptr_t;

// An append-only pool of string that efficiently handles indexing and
// deduplication. Note that an empty pool has a non-negligible cost of ~96KB.
class StringPool {
 public:
  struct Ref {
    Ref() : block_id(0), offset(0) {}
    Ref(uint16_t b, uint16_t o) : block_id(b), offset(o) {}
    uint16_t block_id;
    uint16_t offset;
  };

  StringPool();

  StringId Insert(base::StringView);

  Ref GetNext(Ref ref) const {
    PERFETTO_DCHECK(ref.block_id < num_blocks_);
    const Block& block = *blocks_[ref.block_id];
    Length len;
    memcpy(&len, &block.data[ref.offset], sizeof(Length));
    size_t next_offset =
        static_cast<size_t>(ref.offset) + sizeof(Length) + len + 1;
    if (next_offset < block.offset)
      return Ref(ref.block_id, static_cast<uint16_t>(next_offset));
    if (next_offset == block.offset)
      return Ref(ref.block_id + 1, 0);
    PERFETTO_CHECK(false);
  }

  StringId GetStringId(Ref ref) const {
    PERFETTO_DCHECK(ref.block_id < num_blocks_);
    const Block& block = *blocks_[ref.block_id];
    PERFETTO_DCHECK(ref.offset < block.offset - 2);
    return reinterpret_cast<StringId>(&block.data[ref.offset + 2]);
  }

  const char* GetCStr(StringId string_id) const {
    return reinterpret_cast<const char*>(string_id);
  }

  base::StringView GetStringView(StringId string_id) const {
    const char* raw = reinterpret_cast<const char*>(string_id);
    Length len;
    memcpy(&len, raw - sizeof(Length), sizeof(Length));
    return base::StringView(raw, len);
  }

  size_t size() const { return num_strings_; }

 private:
  // Each string in the pool stored in |data| is prefixed with its len.
  using Length = uint16_t;
  static constexpr unsigned kNumBlocks = 4096;  // ~ 256 MB of strings.

  struct Block {
    Block();
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
