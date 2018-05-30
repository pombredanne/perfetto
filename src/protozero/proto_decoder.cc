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

#include "perfetto/protozero/proto_decoder.h"

#include <cstring>

namespace protozero {

using namespace proto_utils;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BYTE_SWAP_TO_LE32(x) (x)
#define BYTE_SWAP_TO_LE64(x) (x)
#else
#error Unimplemented for big endian archs.
#endif

ProtoDecoder::ProtoDecoder(const uint8_t* buffer, uint64_t length)
    : buffer_(buffer), length_(length) {}

ProtoDecoder::Field ProtoDecoder::ReadField() {
  Field field{};

  // The first byte of a proto field is structured as follows:
  // The least 3 significant bits determine the field type.
  // The most 5 significant bits determine the field id. If MSB == 1, the
  // field id continues on the next bytes following the VarInt encoding.
  const uint8_t kFieldTypeNumBits = 3;
  const uint64_t kFieldTypeMask = (1 << kFieldTypeNumBits) - 1;  // 0000 0111;

  const uint8_t* end = buffer_ + length_;
  const uint8_t* pos = buffer_ + offset_;

  uint64_t raw_field_id;
  pos = ParseVarInt(pos, end, &raw_field_id);

  field.id = static_cast<uint32_t>(raw_field_id >> kFieldTypeNumBits);
  if (field.id == 0 || pos >= end) {
    field.id = 0;
    return field;
  }
  field.type = static_cast<FieldType>(raw_field_id & kFieldTypeMask);

  uint64_t field_intvalue;
  switch (field.type) {
    case kFieldTypeFixed64: {
      if (pos + sizeof(uint64_t) > end) {
        field.id = 0;
        return field;
      }
      memcpy(&field_intvalue, pos, sizeof(uint64_t));
      field.int_value = BYTE_SWAP_TO_LE64(field_intvalue);
      pos += sizeof(uint64_t);
      break;
    }
    case kFieldTypeFixed32: {
      if (pos + sizeof(uint32_t) > end) {
        field.id = 0;
        return field;
      }
      uint32_t tmp;
      memcpy(&tmp, pos, sizeof(uint32_t));
      field.int_value = BYTE_SWAP_TO_LE32(tmp);
      pos += sizeof(uint32_t);
      break;
    }
    case kFieldTypeVarInt: {
      // We need to explicity check for zero to ensure that ParseVarInt doesn't
      // return zero because of running out of space in the buffer.
      if (*pos == 0) {
        pos++;
        field.int_value = 0;
        break;
      }
      pos = ParseVarInt(pos, end, &field.int_value);
      if (field.int_value == 0) {
        field.id = 0;
        return field;
      }
      break;
    }
    case kFieldTypeLengthDelimited: {
      if (*pos == 0) {
        field.length_value.buffer = ++pos;
        field.length_value.length = 0;
        break;
      }
      pos = ParseVarInt(pos, end, &field_intvalue);
      if (field_intvalue == 0 || pos + field_intvalue > end) {
        field.id = 0;
        return field;
      }
      field.length_value.buffer = pos;
      field.length_value.length = field_intvalue;
      pos += field_intvalue;
      break;
    }
  }
  offset_ = static_cast<uint64_t>(pos - buffer_);
  return field;
}

bool ProtoDecoder::IsEndOfBuffer() {
  return length_ == offset_;
}

}  // namespace protozero
