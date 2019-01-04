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

#ifndef INCLUDE_PERFETTO_PROTOZERO_SCATTERED_STREAM_MEMORY_DELEGATE_H_
#define INCLUDE_PERFETTO_PROTOZERO_SCATTERED_STREAM_MEMORY_DELEGATE_H_

#include <memory>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/protozero/scattered_stream_writer.h"

namespace perfetto {

class ScatteredStreamMemoryDelegate
    : public protozero::ScatteredStreamWriter::Delegate {
 public:
  class Chunk {
   public:
    explicit Chunk(size_t size);
    Chunk(Chunk&& chunk);
    ~Chunk();

    protozero::ContiguousMemoryRange GetTotalRange() const;
    protozero::ContiguousMemoryRange GetUsedRange() const;

    uint8_t* start() const { return buffer_.get(); }
    size_t size() const { return size_; }
    size_t unused_bytes() const { return unused_bytes_; }
    void set_unused_bytes(size_t unused_bytes) { unused_bytes_ = unused_bytes; }

   private:
    std::unique_ptr<uint8_t[]> buffer_;
    const size_t size_;
    size_t unused_bytes_;
  };

  ScatteredStreamMemoryDelegate(size_t initial_chunk_size_bytes = 128,
                                size_t maximum_chunk_size_bytes = 128 * 1024);
  ~ScatteredStreamMemoryDelegate() override;

  // protozero::ScatteredStreamWriter::Delegate implementation.
  protozero::ContiguousMemoryRange GetNewBuffer() override;

  // Stitch all the chunks into a single contiguous buffer.
  std::vector<uint8_t> StitchChunks();

  const std::vector<Chunk>& chunks() const { return chunks_; }

  void set_writer(protozero::ScatteredStreamWriter* writer) {
    writer_ = writer;
  }

  // Update unused_bytes() of the current |Chunk| based on the writer's state.
  void AdjustUsedSizeOfCurrentChunk();

  // Returns the total size the chunks occupy in heap memory (including unused).
  size_t GetTotalSize();

 private:
  size_t next_chunk_size_;
  const size_t maximum_chunk_size_;
  protozero::ScatteredStreamWriter* writer_ = nullptr;
  std::vector<Chunk> chunks_;
};

}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_PROTOZERO_SCATTERED_STREAM_MEMORY_DELEGATE_H_
