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

#ifndef SRC_PROFILING_MEMORY_SHARED_RING_BUFFER_H_
#define SRC_PROFILING_MEMORY_SHARED_RING_BUFFER_H_

#include "perfetto/base/optional.h"
#include "perfetto/base/unix_socket.h"
#include "perfetto/base/utils.h"

#include <atomic>
#include <map>
#include <memory>

#include <stdint.h>

namespace perfetto {
namespace profiling {

class ScopedSpinlock {
 public:
  ScopedSpinlock(std::atomic<bool>* lock, bool force);
  ~ScopedSpinlock();

  void Unlock();
  bool locked() const;

 private:
  std::atomic<bool>* const lock_;
  bool locked_ = false;
};

// A concurrent, multi-writer single-reader ring buffer FIFO, based on a
// circular buffer over shared memory. It has similar semantics to a SEQ_PACKET
// + O_NONBLOCK socket, specifically: - Writes are atomic, data is either
// written fully in the buffer or not. - New writes are discarded if the buffer
// is full. - If a write succeeds, the reader is guaranteed to see the whole
// buffer. - Reads are atomic, no fragmentation. - The reader sees writes in
// write order (% discarding).
//
// This class assumes that reader and write trust each other. Don't use in
// untrusted contexts.
//
// TODO:
// - Write a benchmark.
// - Make the stats ifdef-able.
class SharedRingBuffer {
 public:
  class WriteBuffer {
   public:
    friend class SharedRingBuffer;
    uint8_t* buf();
    size_t size() { return size_; }

    operator bool() { return wr_ptr_ != nullptr; }

   private:
    size_t size_;
    uint8_t* wr_ptr_ = nullptr;
    uint64_t write_pos_;
    size_t size_with_header_;
  };

  class ReadBuffer {
   public:
    friend class SharedRingBuffer;
    friend void swap(ReadBuffer&, ReadBuffer&);

    ReadBuffer() = default;
    ~ReadBuffer();

    ReadBuffer(const ReadBuffer&) = delete;
    ReadBuffer& operator=(const ReadBuffer&) = delete;

    ReadBuffer(ReadBuffer&&) noexcept;
    ReadBuffer& operator=(ReadBuffer&&) noexcept;

    uint8_t* payload() const { return data_; }
    size_t payload_size() const { return size_; }
    operator bool() const { return ring_buffer_ != nullptr; }

   private:
    ReadBuffer(uint8_t* data,
               size_t size,
               size_t size_with_header,
               SharedRingBuffer* ring_buffer)
        : data_(data),
          size_(size),
          size_with_header_(size_with_header),
          ring_buffer_(ring_buffer) {}

    uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t size_with_header_ = 0;
    SharedRingBuffer* ring_buffer_ = nullptr;
  };

  static base::Optional<SharedRingBuffer> Create(size_t);
  static base::Optional<SharedRingBuffer> Attach(base::ScopedFile);

  ~SharedRingBuffer();
  SharedRingBuffer(SharedRingBuffer&&) noexcept;
  SharedRingBuffer& operator=(SharedRingBuffer&&);

  bool is_valid() const { return !!mem_; }
  size_t size() const { return size_; }
  int fd() const { return *mem_fd_; }

  WriteBuffer PrepareWrite(size_t size);
  void EndWrite(const WriteBuffer& buf);

  bool TryWrite(const void*, size_t) PERFETTO_WARN_UNUSED_RESULT;
  ReadBuffer Read();

 private:
  struct alignas(base::kPageSize) MetadataPage {
    std::atomic<bool> spinlock;
    uint64_t read_pos;
    uint64_t write_pos;
    uint64_t written_pos;

    // stats, for debugging only.
    std::atomic<uint64_t> failed_spinlocks;
    std::atomic<uint64_t> bytes_written;
    std::atomic<uint64_t> num_writes_succeeded;
    std::atomic<uint64_t> num_writes_failed;
    std::atomic<uint64_t> num_reads_failed;

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
  void Return(const ReadBuffer&);

  // Must be called holding the spinlock.
  inline size_t read_avail() {
    PERFETTO_DCHECK(meta_->written_pos >= meta_->read_pos);
    auto res = static_cast<size_t>(meta_->written_pos - meta_->read_pos);
    PERFETTO_DCHECK(res <= size_);
    return res;
  }

  // Must be called holding the spinlock.
  inline size_t write_avail() {
    return size_ - (meta_->write_pos - meta_->read_pos);
  }

  inline uint8_t* at(uint64_t pos) { return mem_ + (pos & (size_ - 1)); }

  base::ScopedFile mem_fd_;
  MetadataPage* meta_ = nullptr;  // Start of the mmaped region.
  uint8_t* mem_ = nullptr;  // Start of the contents (i.e. meta_ + kPageSize).

  // Size of the ring buffer contents, without including metadata or the 2nd
  // mmap.
  size_t size_ = 0;

  // Remember to update the move ctor when adding new fields.
};

void swap(SharedRingBuffer::ReadBuffer&, SharedRingBuffer::ReadBuffer&);

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_SHARED_RING_BUFFER_H_
