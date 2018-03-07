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

#ifndef SRC_TRACING_CORE_PATCH_LIST_H_
#define SRC_TRACING_CORE_PATCH_LIST_H_

#include <array>
#include <forward_list>

#include "perfetto/tracing/core/basic_types.h"

namespace perfetto {

// Used to handle the backfilling of the headers (the |size_field|) of nested
// messages when a proto is fragmented over several chunks. These patches are
// sent out-of-band to the tracing service, after having returned the initial
// chunks of the fragment.
class Patch {
 public:
  using PatchContent = std::array<uint8_t, SharedMemoryABI::kPacketHeaderSize>;
  Patch(ChunkID c, uint16_t o) : chunk_id(c), offset_in_chunk(o) {}

  const ChunkID chunk_id;
  const uint16_t offset_in_chunk;
  PatchContent size_field{};

  // |size_field| contains a varint. Any varint must start with != 0. Even in
  // the case we want to encode a size == 0, protozero will write a rendundant
  // varint for that, that is [0x80, 0x80, 0x80, 0x00]. So the first byte is 0
  // iff we never wrote any varint into that.
  bool is_patched() const { return size_field[0] != 0; }

 private:
  Patch(const Patch&) = delete;
  Patch& operator=(const Patch&) = delete;
  Patch(Patch&&) noexcept = delete;
  Patch& operator=(Patch&&) = delete;
};

// Note: the protozero::Message(s) will take pointers to the |size_field| of
// these entries. This container must guarantee that the Patch objects are never
// moved around (i.e. cannot be a vector because of reallocations can change
// addresses of pre-existing entries).
class PatchList : public std::forward_list<Patch> {
 public:
  PatchList() : std::forward_list<Patch>(), last_(begin()) {}

  Patch* emplace_back(ChunkID chunk_id, uint16_t offset) {
    last_ = emplace_after(last_, chunk_id, offset);
    return &*last_;
  }

 private:
  std::forward_list<Patch>::iterator last_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_CORE_PATCH_LIST_H_
