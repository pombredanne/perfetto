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

#ifndef SRC_TRACING_CORE_PACKET_STREAM_VALIDATOR_H_
#define SRC_TRACING_CORE_PACKET_STREAM_VALIDATOR_H_

#include <inttypes.h>
#include <stddef.h>
#include "src/tracing/core/chunked_protobuf_input_stream.h"

namespace perfetto {

// Checks that the stream of trace packets sent by the producer is well formed.
class PacketStreamValidator {
 public:
  explicit PacketStreamValidator(const ChunkSequence* sequence);

  bool Validate();

 private:
  bool ReadByte(uint8_t* value);
  bool SkipBytes(size_t count);
  bool ConsumeField(uint64_t* field_id);
  bool ConsumeVarInt(uint64_t* value);
  bool Eof() const;

  ChunkedProtobufInputStream stream_;
  size_t total_size_ = 0;
  size_t read_size_ = 0;

  const uint8_t* pos_ = nullptr;
  int size_ = 0;
};

}  // namespace perfetto

#endif  // SRC_TRACING_CORE_PACKET_STREAM_VALIDATOR_H_
