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

#include "src/heapprofd/record_reader.h"

#include "perfetto/base/logging.h"

#include <unistd.h>
#include <algorithm>

namespace perfetto {
namespace {
constexpr uint64_t kChunkSize = 16u * 4096u;
}

RecordReader::RecordReader(
    std::function<void(size_t, std::unique_ptr<uint8_t[]>)> callback_function)
    : callback_function_(callback_function) {
  Reset();
}

ssize_t RecordReader::Read(int fd) {
  if (read_idx_ < sizeof(record_size_)) {
    ssize_t rd = ReadRecordSize(fd);
    if (rd != -1) {
      PERFETTO_DCHECK(rd >= 0);
      read_idx_ += static_cast<size_t>(rd);
    }
    if (read_idx_ == sizeof(record_size_)) {
      buf_.reset(new uint8_t[record_size_]);
      // Make sure zero sized records don't make us return zero from this
      // method which gets interpreted as fd closed.
      //
      // Without this check, the caller would re-enter in here, and ReadRecord
      // would be called with a record_size_ of zero, which will make read(2)
      // return 0.
      MaybeCallback();
    }
    return rd;
  }

  ssize_t rd = ReadRecord(fd);
  if (rd != -1) {
    PERFETTO_DCHECK(rd >= 0);
    read_idx_ += static_cast<size_t>(rd);
  }
  MaybeCallback();
  return rd;
}

void RecordReader::MaybeCallback() {
  if (done()) {
    callback_function_(record_size_, std::move(buf_));
    Reset();
  }
}

void RecordReader::Reset() {
  read_idx_ = 0;
  record_size_ = 0;
}

bool RecordReader::done() {
  return read_idx_ >= sizeof(record_size_) &&
         read_idx_ - sizeof(record_size_) == record_size_;
}

size_t RecordReader::read_idx() {
  if (read_idx_ < sizeof(record_size_))
    return read_idx_;
  return read_idx_ - sizeof(record_size_);
}

ssize_t RecordReader::ReadRecordSize(int fd) {
  ssize_t rd = PERFETTO_EINTR(
      read(fd, reinterpret_cast<uint8_t*>(&record_size_) + read_idx_,
           sizeof(record_size_) - read_idx_));
  if (rd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    PERFETTO_PLOG("read record size (fd: %d)", fd);
    PERFETTO_DCHECK(false);
  }
  return rd;
}

ssize_t RecordReader::ReadRecord(int fd) {
  uint64_t sz = std::min(kChunkSize, record_size_ - read_idx());
  ssize_t rd = read(fd, buf_.get() + read_idx(), sz);
  if (rd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    PERFETTO_PLOG("read record (fd: %d)", fd);
    PERFETTO_DCHECK(false);
  }
  return rd;
}

}  // namespace perfetto
