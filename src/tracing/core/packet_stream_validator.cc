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
#include "perfetto/trace/trusted_packet.pb.h"

#include "src/tracing/core/chunked_protobuf_input_stream.h"

namespace perfetto {

PacketStreamValidator::PacketStreamValidator(const ChunkSequence* sequence)
    : stream_(sequence) {
  static_assert(protos::TracePacket::kTrustedUidFieldNumber ==
                    protos::TrustedPacket::kTrustedUidFieldNumber,
                "trusted uid field id mismatch");
  for (const Chunk& chunk : *sequence)
    size_ += chunk.size;
}

bool PacketStreamValidator::Validate() {
  protos::TrustedPacket packet;
  if (!packet.ParseFromBoundedZeroCopyStream(&stream_, size_))
    return false;
  // Only the service is allowed to fill in the trusted uid.
  return !packet.has_trusted_uid();
}

}  // namespace perfetto
