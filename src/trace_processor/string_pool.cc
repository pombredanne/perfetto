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

#include "src/trace_processor/string_pool.h"

#include <string.h>
#include <type_traits>

namespace perfetto {
namespace trace_processor {

StringPool::StringPool() {
  blocks_[0].reset(new Block());
  num_blocks_ = 1;

  // Reserve 0 for the empty string.
  StringId id = Insert("");
  PERFETTO_DCHECK(id == 0);

  static_assert(kNumBlocks <= kMaxBlockId + 1, "kNumBlocks too big");
  static_assert(std::is_trivially_destructible<Block>::value,
                "Block must be trivially destructible");
}

StringId StringPool::Insert(base::StringView str) {
  auto hash = str.Hash();
  auto it = index_.find(hash);
  if (it != index_.end()) {
    PERFETTO_DCHECK(Get(it->second) == str);
    return it->second;
  }

  size_t size_required = sizeof(Length) + str.size() + 1 /* null terminator */;
  PERFETTO_CHECK(size_required <= sizeof(Block::data));

  for (;;) {
    Block& block = *blocks_[num_blocks_ - 1];
    if (size_required > block.space_left()) {
      PERFETTO_CHECK(num_blocks_ < kNumBlocks);
      blocks_[num_blocks_++].reset(new Block());
      PERFETTO_CHECK(blocks_[num_blocks_ - 1]->space_left() >= size_required);
      continue;
    }
    auto len = static_cast<Length>(str.size());
    char* data = &block.data[block.offset];

    // Write first the length of the string.
    memcpy(data, &len, sizeof(Length));
    data += sizeof(Length);

    // Then the actual string itself.
    memcpy(data, str.data(), str.size());
    data += str.size();

    // Then the null terminator.
    *(data++) = '\0';

    static_assert(kMaxOffset >= sizeof(block.data), "kOffsetBits too small");

    StringId string_id = static_cast<StringId>(
        block.offset | ((num_blocks_ - 1) << kOffsetBits));

    PERFETTO_DCHECK(static_cast<size_t>(data - &block.data[block.offset]) ==
                    size_required);
    block.offset += size_required;

    index_.emplace(hash, string_id);
    num_strings_++;
    return string_id;
  }
}

}  // namespace trace_processor
}  // namespace perfetto
