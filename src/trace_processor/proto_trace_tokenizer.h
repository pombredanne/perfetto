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

#ifndef SRC_TRACE_PROCESSOR_PROTO_TRACE_TOKENIZER_H_
#define SRC_TRACE_PROCESSOR_PROTO_TRACE_TOKENIZER_H_

#include <stdint.h>
#include <memory>

#include "src/trace_processor/chunk_reader.h"
#include "src/trace_processor/trace_blob_view.h"

namespace perfetto {
namespace trace_processor {

class BlobReader;
class TraceProcessorContext;

// Reads a protobuf trace in chunks and extracts the timestamp.
class ProtoTraceTokenizer : public ChunkReader {
 public:
  // |reader| is the abstract method of getting chunks of size |chunk_size_b|
  // from a trace file with these chunks parsed into |trace|.
  ProtoTraceTokenizer(BlobReader*, TraceProcessorContext*);
  ~ProtoTraceTokenizer() override;

  // ChunkReader implementation.

  // Parses the next chunk of TracePackets from the BlobReader. Returns true
  // if there are more chunks which can be read and false otherwise.
  bool ParseNextChunk() override;

  void set_chunk_size_for_testing(uint32_t n) { chunk_size_ = n; }

 private:
  static constexpr uint32_t kTraceChunkSize = 16 * 1024 * 1024;  // 16 MB

  void ParsePacket(const uint8_t* data,
                   size_t length,
                   std::shared_ptr<uint8_t> buffer);
  void ParseFtraceEventBundle(const uint8_t* data,
                              size_t length,
                              std::shared_ptr<uint8_t> buffer);
  void ParseFtraceEvent(uint32_t cpu,
                        const uint8_t* data,
                        size_t length,
                        std::shared_ptr<uint8_t> buffer);

  BlobReader* const reader_;
  TraceProcessorContext* context_;

  // Temporary - currently trace packets do not have a timestamp, so the
  // timestamp given is last_timestamp + 1.
  uint64_t last_timestamp = 0;
  uint32_t chunk_size_ = kTraceChunkSize;
  uint64_t offset_ = 0;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_PROTO_TRACE_TOKENIZER_H_
