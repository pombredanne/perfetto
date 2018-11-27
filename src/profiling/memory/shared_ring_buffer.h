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

#ifndef SRC_PROFILING_MEMORY_SOCKET_LISTENER_H_
#define SRC_PROFILING_MEMORY_SOCKET_LISTENER_H_

#include "perfetto/base/optional.h"
#include "perfetto/base/unix_socket.h"
#include "perfetto/base/utils.h"

#include <atomic>
#include <map>
#include <memory>

#include <stdint.h>

namespace perfetto {
namespace profiling {

class SharedRingBuffer {
 public:
  using BufferAndSize = std::pair<std::unique_ptr<uint8_t[]>, size_t>;
  static base::Optional<SharedRingBuffer> Create(size_t);
  static base::Optional<SharedRingBuffer> Attach(base::ScopedFile);

  ~SharedRingBuffer();
  SharedRingBuffer(SharedRingBuffer&&) noexcept;
  SharedRingBuffer& operator=(SharedRingBuffer&&);

  bool is_valid() const { return !!mem_; }
  size_t size() const { return size_; }
  int fd() const { return *mem_fd_; }
  bool Write(const void*, size_t) PERFETTO_WARN_UNUSED_RESULT;
  BufferAndSize Read();

 private:
  struct alignas(base::kPageSize) MetadataPage {
    std::atomic<bool> spinlock;
    uint64_t read_pos;
    uint64_t write_pos;

    // stats, for debugging only.
    uint64_t bytes_written;
    uint64_t num_writes_succeeded;
    uint64_t num_writes_failed;
    uint64_t num_reads_failed;

    // TODO static assert offsets.
  };

  struct CreateFlag {};
  struct AttachFlag {};
  SharedRingBuffer(const SharedRingBuffer&) = delete;
  SharedRingBuffer& operator=(const SharedRingBuffer&) = delete;
  SharedRingBuffer(CreateFlag, size_t size);
  SharedRingBuffer(AttachFlag, base::ScopedFile mem_fd);
  void Initialize(base::ScopedFile mem_fd);
  bool IsCorrupt();

  base::ScopedFile mem_fd_;
  MetadataPage* meta_ = nullptr;
  uint8_t* mem_ = nullptr;  // Start of the contents (i.e. meta_ + kPageSize).

  // Size of the ring buffer contents, without including metadata or the 2nd
  // mmap.
  size_t size_ = 0;

  // Remember to update the move ctor when adding new fields.
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_SOCKET_LISTENER_H_
