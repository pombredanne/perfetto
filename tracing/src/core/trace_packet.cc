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

#include "tracing/core/trace_packet.h"

#include "protos/trace_packet.pb.h"

namespace perfetto {

TracePacket::TracePacket(const void* start, size_t size)
    : start_(start), size_(size) {}

TracePacket::~TracePacket() = default;

TracePacket::TracePacket(TracePacket&&) noexcept = default;
TracePacket& TracePacket::operator=(TracePacket&&) = default;

bool TracePacket::Decode() {
  if (decoded_packet_)
    return true;
  decoded_packet_.reset(new DecodedTracePacket());
  if (!decoded_packet_->ParseFromArray(start_, static_cast<int>(size_))) {
    decoded_packet_.reset();
    return false;
  }
  return true;
}

}  // namespace perfetto
