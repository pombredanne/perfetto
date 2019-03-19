/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {

StringPool::StringPool() {
  blocks_.emplace_back();

  // Reserve one byte for the null string. This prevents any other string
  // being given id 0.
  PERFETTO_CHECK(blocks_.back().Reserve(1));
}

StringPool::~StringPool() = default;

StringPool::StringPool(StringPool&&) = default;
StringPool& StringPool::operator=(StringPool&&) = default;

StringPool::Id StringPool::InsertString(base::StringView str, uint64_t hash) {
  // We shouldn't be writing string with more than 2^16 characters to the pool.
  PERFETTO_DCHECK(str.size() < std::numeric_limits<uint16_t>::max());
  uint16_t str_size = static_cast<uint16_t>(str.size());

  // Try and find enough space in the current block for the string and the
  // metadata (the size of the string + the null terminator + 2 bytes to encode
  // the size itself).
  auto* ptr = blocks_.back().Reserve(kMetadataSize + str_size);
  if (PERFETTO_UNLIKELY(!ptr)) {
    // This means the block did not have enough space. This should only happen
    // on 32-bit platforms as we allocate a 4GB mmap on 64 bit.
    PERFETTO_CHECK(sizeof(uint8_t*) == 4);

    // Add a new block to store the data.
    blocks_.emplace_back();

    // Try and reserve space again - this time we should definitely succeed.
    ptr = blocks_.back().Reserve(kMetadataSize + str_size);
    PERFETTO_CHECK(ptr);
  }

  // First memcpy the size of the string into the buffer.
  memcpy(ptr, &str_size, sizeof(str_size));

  // Next the string itself which starts at offset 2.
  memcpy(&ptr[2], str.data(), str_size);

  // Finally add a null terminator.
  ptr[2 + str_size] = '\0';

  // Finish by computing the id of the pointer and adding a mapping from the
  // hash to the string_id.
  Id string_id = PtrToId(ptr);
  string_index_.emplace(hash, string_id);
  return string_id;
}

StringPool::Iterator::Iterator(const StringPool* pool) : pool_(pool) {}

bool StringPool::Iterator::Next() {
  PERFETTO_DCHECK(block_id_ < pool_->blocks_.size());

  // On the first call to next, always return true as we always have the null
  // string.
  if (PERFETTO_UNLIKELY(first_)) {
    first_ = false;
    return true;
  }

  // Otherwise, try and go to the next string in the current block.
  const auto& block = pool_->blocks_[block_id_];
  if (PERFETTO_UNLIKELY(block_id_ == 0 && block_offset_ == 0)) {
    // The null string is at (0, 0). The next string is simply the same block
    // at offset 1.
    block_offset_ = 1;
  } else {
    // Otherwise, find the size of the string at the current offset in the block
    // and increment the offset by that size.
    auto str_size = GetSize(block.Get(block_offset_));
    block_offset_ += kMetadataSize + str_size;
  }

  // If we're out of bounds for this block, go the the start of the next block.
  if (block.pos() <= block_offset_) {
    block_id_++;
    block_offset_ = 0;
    return block_id_ < pool_->blocks_.size();
  }
  return true;
}

NullTermStringView StringPool::Iterator::StringView() {
  PERFETTO_DCHECK(block_id_ < pool_->blocks_.size());
  PERFETTO_DCHECK(block_offset_ < pool_->blocks_[block_id_].pos());

  // If we're at (0, 0), we have the null string.
  if (block_id_ == 0 && block_offset_ == 0)
    return NullTermStringView();
  return GetFromPtr(pool_->blocks_[block_id_].Get(block_offset_));
}

StringPool::Id StringPool::Iterator::StringId() {
  PERFETTO_DCHECK(block_id_ < pool_->blocks_.size());
  PERFETTO_DCHECK(block_offset_ < pool_->blocks_[block_id_].pos());

  // If we're at (0, 0), we have the null string which has id 0.
  if (block_id_ == 0 && block_offset_ == 0)
    return 0;
  return pool_->PtrToId(pool_->blocks_[block_id_].Get(block_offset_));
}

}  // namespace trace_processor
}  // namespace perfetto
