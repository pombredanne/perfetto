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

#include "perfetto/tracing/core/trace_packet.h"
#include "src/tracing/core/trace_buffer.h"

namespace perfetto {
namespace {

// This fuzzer mimics a malicious producer alternating random IPCs and data.
void FuzzTraceBuffer(const uint8_t* data, size_t size) {
  const uint8_t* end = data + size;
  const uint8_t* ptr = data;
  auto trace_buffer = TraceBuffer::Create(4096 * 16);

  constexpr size_t kMaxChunkSize = 1 << (8 * sizeof(uint16_t));
  std::unique_ptr<uint8_t[]> chunk_payload(new uint8_t[kMaxChunkSize]);

  // The contents of the copied buffer itself shouln't matter. It should only
  // be copied into and out of the trace buffer. If any buffer overflow happens
  // asan will detect it.
  memset(&chunk_payload[0], 0xff, kMaxChunkSize);

  // The UID doesn't have any effect on the control flow and is only copied back
  // when rading packets back. Also, conversely to the other arguments, it
  // cannot be spoofed by a malicious producer. There is no point adding another
  // dimension to the state space, we hardcode it instead.
  const uid_t kUid = 42;

  // 32 is an upper bound on the number of random bytes required by the fuzzer
  // harness in each iteration of the while loop.
  while (ptr < end - 32) {
    uint8_t rnd = *(ptr++) % 4;
    enum { kCopyChunk, kPatchChunk, kReadChunk } action;
    action = rnd < 2 ? kCopyChunk : (rnd == 2 ? kPatchChunk : kReadChunk);

    switch (action) {
      case kCopyChunk: {
        ProducerID producer_id;
        memcpy(&producer_id, ptr, sizeof(producer_id));
        ptr += sizeof(producer_id);

        WriterID writer_id;
        memcpy(&writer_id, ptr, sizeof(writer_id));
        ptr += sizeof(writer_id);

        ChunkID chunk_id;
        memcpy(&chunk_id, ptr, sizeof(chunk_id));
        ptr += sizeof(chunk_id);

        uint16_t num_fragments;
        memcpy(&num_fragments, ptr, sizeof(num_fragments));
        ptr += sizeof(num_fragments);

        uint8_t chunk_flags;
        memcpy(&chunk_flags, ptr, sizeof(chunk_flags));
        ptr += sizeof(chunk_flags);

        uint16_t chunk_size;
        memcpy(&chunk_size, ptr, sizeof(chunk_size));
        ptr += sizeof(chunk_size);

        const size_t kPatternSize = 16;
        const uint8_t* pattern = ptr;
        ptr += kPatternSize;

        for (size_t i = 0; i <= chunk_size; i += kPatternSize)
          memcpy(&chunk_payload[0], pattern, kPatternSize);

        trace_buffer->CopyChunkUntrusted(producer_id, kUid, writer_id, chunk_id,
                                         num_fragments, chunk_flags,
                                         &chunk_payload[0], chunk_size);
        break;
      }

      case kPatchChunk: {
        ProducerID producer_id;
        memcpy(&producer_id, ptr, sizeof(producer_id));
        ptr += sizeof(producer_id);

        WriterID writer_id;
        memcpy(&writer_id, ptr, sizeof(writer_id));
        ptr += sizeof(writer_id);

        ChunkID chunk_id;
        memcpy(&chunk_id, ptr, sizeof(chunk_id));
        ptr += sizeof(chunk_id);

        rnd = *(ptr++);
        bool other_patches_pending = (rnd & 0x80) == 0;
        size_t num_patches = (rnd % 4) + 1;

        std::vector<TraceBuffer::Patch> patches;
        const size_t avail = static_cast<size_t>(end - ptr);
        const size_t kPatchSize = sizeof(uint16_t) + TraceBuffer::Patch::kSize;
        num_patches = std::min(num_patches, avail / kPatchSize);

        for (size_t i = 0; i < num_patches; i++) {
          uint16_t patch_offset;
          memcpy(&patch_offset, ptr, sizeof(patch_offset));
          ptr += sizeof(patch_offset);
          patches.emplace_back();
          patches.back().offset_untrusted = patch_offset;
          memcpy(&patches.back().data, ptr, sizeof(patches.back().data));
          ptr += sizeof(patches.back().data);
        }
        trace_buffer->TryPatchChunkContents(producer_id, writer_id, chunk_id,
                                            patches.data(), patches.size(),
                                            other_patches_pending);
        break;
      }

      case kReadChunk: {
        uint16_t max_reads;
        memcpy(&max_reads, ptr, sizeof(max_reads));
        ptr += sizeof(max_reads);
        trace_buffer->BeginRead();
        for (size_t i = 0; i < max_reads; i++) {
          TracePacket tp;
          uid_t uid_read;
          if (!trace_buffer->ReadNextTracePacket(&tp, &uid_read))
            break;
          PERFETTO_CHECK(uid_read == kUid);
        }
        break;
      }
    }  // switch(action)
  }    // while(ptr < end - X)
}

}  // namespace
}  // namespace perfetto

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  perfetto::FuzzTraceBuffer(data, size);
  return 0;
}
