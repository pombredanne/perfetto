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

#include "src/tracing/core/packet_stream_validator.h"

#include "perfetto/base/logging.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/trace/trace_packet.pb.h"

using protozero::proto_utils::FieldType;
using protozero::proto_utils::kFieldTypeVarInt;
using protozero::proto_utils::kFieldTypeFixed64;
using protozero::proto_utils::kFieldTypeLengthDelimited;
using protozero::proto_utils::kFieldTypeFixed32;

namespace perfetto {

PacketStreamValidator::PacketStreamValidator(const ChunkSequence* sequence)
    : stream_(sequence) {
  for (const Chunk& chunk : *sequence)
    total_size_ += chunk.size;
}

bool PacketStreamValidator::Validate() {
  while (!Eof()) {
    uint64_t field_id;
    if (!ConsumeField(&field_id))
      return false;

    // Only the service is allowed to emit the trusted uid field.
    if (field_id == protos::TracePacket::kTrustedUidFieldNumber)
      return false;
  }
  return true;
}

bool PacketStreamValidator::ReadByte(uint8_t* value) {
  while (size_ < 1) {
    if (!stream_.Next(reinterpret_cast<const void**>(&pos_), &size_))
      return false;
  }
  *value = *pos_++;
  size_--;
  read_size_++;
  return true;
}

bool PacketStreamValidator::Eof() const {
  PERFETTO_DCHECK(read_size_ <= total_size_);
  return read_size_ == total_size_;
}

bool PacketStreamValidator::SkipBytes(size_t count) {
  read_size_ += count;
  while (count > 0 && size_ > 0) {
    pos_++;
    size_--;
    count--;
  }
  if (!size_ && count) {
    pos_ = nullptr;
    return stream_.Skip(count) || Eof();
  }
  return true;
}

bool PacketStreamValidator::ConsumeVarInt(uint64_t* value) {
  uint64_t shift = 0;
  *value = 0;
  uint8_t byte;
  do {
    if (!ReadByte(&byte))
      return false;
    PERFETTO_DCHECK(shift < 64ull);
    *value |= static_cast<uint64_t>(byte & 0x7f) << shift;
    shift += 7;
  } while (byte & 0x80);
  return true;
}

bool PacketStreamValidator::ConsumeField(uint64_t* field_id) {
  if (!ConsumeVarInt(field_id))
    return false;

  const uint8_t kFieldTypeNumBits = 3;
  const uint8_t kFieldTypeMask = (1 << kFieldTypeNumBits) - 1;  // 0000 0111;
  int field_type = static_cast<FieldType>(*field_id & kFieldTypeMask);

  *field_id >>= kFieldTypeNumBits;
  PERFETTO_DCHECK(*field_id <= std::numeric_limits<uint32_t>::max());

  switch (field_type) {
    case kFieldTypeFixed64: {
      if (!SkipBytes(8))
        return false;
      break;
    }
    case kFieldTypeFixed32: {
      if (!SkipBytes(4))
        return false;
      break;
    }
    case kFieldTypeVarInt: {
      uint64_t unused;
      if (!ConsumeVarInt(&unused))
        return false;
      break;
    }
    case kFieldTypeLengthDelimited: {
      uint64_t length;
      if (!ConsumeVarInt(&length))
        return false;
      if (!SkipBytes(length))
        return false;
      break;
    }
    default:
      return false;
  }
  return true;
}

}  // namespace perfetto
