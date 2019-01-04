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

#include "perfetto/protozero/scattered_stream_memory_delegate.h"

namespace perfetto {

ScatteredStreamMemoryDelegate::Chunk::Chunk(size_t size)
    : buffer_(std::unique_ptr<uint8_t[]>(new uint8_t[size])),
      size_(size),
      unused_bytes_(size) {
  PERFETTO_DCHECK(size);
#if PERFETTO_DCHECK_IS_ON()
  uint8_t* begin = buffer_.get();
  memset(begin, 0xff, size_);
#endif  // PERFETTO_DCHECK_IS_ON()
}

ScatteredStreamMemoryDelegate::Chunk::Chunk(Chunk&& chunk)
    : buffer_(std::move(chunk.buffer_)),
      size_(std::move(chunk.size_)),
      unused_bytes_(std::move(chunk.unused_bytes_)) {}

ScatteredStreamMemoryDelegate::Chunk::~Chunk() = default;

protozero::ContiguousMemoryRange
ScatteredStreamMemoryDelegate::Chunk::GetTotalRange() const {
  protozero::ContiguousMemoryRange range = {buffer_.get(),
                                            buffer_.get() + size_};
  return range;
}

protozero::ContiguousMemoryRange
ScatteredStreamMemoryDelegate::Chunk::GetUsedRange() const {
  protozero::ContiguousMemoryRange range = {
      buffer_.get(), buffer_.get() + size_ - unused_bytes_};
  PERFETTO_CHECK(range.size() <= size_);
  return range;
}

ScatteredStreamMemoryDelegate::ScatteredStreamMemoryDelegate(
    size_t initial_chunk_size_bytes,
    size_t maximum_chunk_size_bytes)
    : next_chunk_size_(initial_chunk_size_bytes),
      maximum_chunk_size_(maximum_chunk_size_bytes) {
  PERFETTO_DCHECK(next_chunk_size_ && maximum_chunk_size_);
}

ScatteredStreamMemoryDelegate::~ScatteredStreamMemoryDelegate() = default;

protozero::ContiguousMemoryRange ScatteredStreamMemoryDelegate::GetNewBuffer() {
  PERFETTO_CHECK(writer_);
  AdjustUsedSizeOfCurrentChunk();

  chunks_.emplace_back(next_chunk_size_);
  next_chunk_size_ = std::min(maximum_chunk_size_, next_chunk_size_ * 2);
  return chunks_.back().GetTotalRange();
}

std::vector<uint8_t> ScatteredStreamMemoryDelegate::StitchChunks() {
  AdjustUsedSizeOfCurrentChunk();
  std::vector<uint8_t> buffer;
  size_t i = 0;
  for (const auto& chunk : chunks_) {
    auto used_range = chunk.GetUsedRange();
    buffer.insert(buffer.end(), used_range.begin, used_range.end);
    i++;
  }
  return buffer;
}

void ScatteredStreamMemoryDelegate::AdjustUsedSizeOfCurrentChunk() {
  if (!chunks_.empty()) {
    PERFETTO_DCHECK(writer_);
    chunks_.back().set_unused_bytes(writer_->bytes_available());
  }
}

size_t ScatteredStreamMemoryDelegate::GetTotalSize() {
  size_t total_size = 0;
  for (auto& chunk : chunks_) {
    total_size += chunk.size();
  }
  return total_size;
}

}  // namespace perfetto
