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

#include "perfetto/tracing/core/trace_packet.h"

#include "perfetto/base/logging.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/tracing/core/sliced_protobuf_input_stream.h"

#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"

namespace perfetto {

TracePacket::TracePacket() = default;
TracePacket::~TracePacket() = default;

TracePacket::TracePacket(TracePacket&&) noexcept = default;
TracePacket& TracePacket::operator=(TracePacket&&) = default;

bool TracePacket::Decode() {
  if (decoded_packet_)
    return true;
  decoded_packet_.reset(new DecodedTracePacket());
  SlicedProtobufInputStream istr(&slices_);
  if (!decoded_packet_->ParseFromZeroCopyStream(&istr)) {
    decoded_packet_.reset();
    return false;
  }
  return true;
}

void TracePacket::AddSlice(Slice slice) {
  size_ += slice.size;
  slices_.push_back(std::move(slice));
}

void TracePacket::AddSlice(const void* start, size_t size) {
  size_ += size;
  slices_.emplace_back(start, size);
}

std::tuple<char*, size_t> TracePacket::GetPreamble() {
  using protozero::proto_utils::MakeTagLengthDelimited;
  using protozero::proto_utils::WriteVarInt;
  uint32_t tag = MakeTagLengthDelimited(protos::Trace::kPacketFieldNumber);
  PERFETTO_DCHECK(tag < 0x80);  // Should fit in one byte.
  char* ptr = &preamble_[0];
  *(ptr++) = static_cast<char>(tag);
  ptr = reinterpret_cast<char*>(
      WriteVarInt(size(), reinterpret_cast<uint8_t*>(ptr)));
  size_t preamble_size = static_cast<size_t>(ptr - &preamble_[0]);
  PERFETTO_DCHECK(preamble_size <= sizeof(preamble_));
  return std::make_tuple(&preamble_[0], preamble_size);
}

}  // namespace perfetto
