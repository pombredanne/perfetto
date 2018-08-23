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

#ifndef SRC_TRACE_PROCESSOR_TRACE_BLOB_VIEW_H_
#define SRC_TRACE_PROCESSOR_TRACE_BLOB_VIEW_H_

#include <stddef.h>
#include <stdint.h>
#include <memory>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {

// A view of the trace. The buffer is in a shared_ptr so it will be freed
// when all of the TraceBlobViews have passed through the pipeline and been
// parsed.
class TraceBlobView {
 public:
  TraceBlobView(const std::shared_ptr<uint8_t[]>& buffer,
                size_t offset,
                size_t length)
      : buffer_(buffer), offset_(offset), length_(length) {}

  TraceBlobView(TraceBlobView&&) = default;
  TraceBlobView& operator=(TraceBlobView&&) = default;
  TraceBlobView(TraceBlobView const&) = delete;
  TraceBlobView& operator=(TraceBlobView const&) = delete;

  bool operator==(const TraceBlobView& rhs) const {
    return (buffer_ == rhs.buffer_) && (offset_ == rhs.offset_) &&
           (length_ == rhs.length_);
  }
  bool operator!=(const TraceBlobView& rhs) const { return !(*this == rhs); }

  const uint8_t* data() const { return buffer_.get() + offset_; }

  size_t offset_of(const uint8_t* data) const {
    PERFETTO_DCHECK(data >= buffer_.get() &&
                    data < (buffer_.get() + offset_ + length_));
    return static_cast<size_t>(data - buffer_.get());
  }

  const std::shared_ptr<uint8_t[]>& buffer() const { return buffer_; }

  size_t length() const { return length_; }

 private:
  std::shared_ptr<uint8_t[]> buffer_;
  size_t offset_;
  // Length of the particular field (not the whole buffer)
  size_t length_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TRACE_BLOB_VIEW_H_
