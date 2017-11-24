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

#ifndef TRACING_INCLUDE_TRACING_TRACE_PACKET_H_
#define TRACING_INCLUDE_TRACING_TRACE_PACKET_H_

#include <stddef.h>

#include <memory>

class TracePacket;

namespace perfetto {
namespace protos {
class TracePacket;
}  // namespace protos

// A wrapper around a byte buffer that contains a protobuf-encoded TracePacket
// (see trace_packet.proto). The TracePacket is decoded only if the Consumer
// requests that. This is to allow Consumer(s) to just stream the packet over
// the network or save it to a file without wasting time decoding it.
class TracePacket {
 public:
  using DecodedTracePacket = protos::TracePacket;
  TracePacket(const void* start, size_t size);
  ~TracePacket();
  TracePacket(TracePacket&&) noexcept;
  TracePacket& operator=(TracePacket&&);

  const void* start() const { return start_; }
  size_t size() const { return size_; }

  bool Decode();

  // Must explicitly call Decode() first.
  const DecodedTracePacket* operator->() {
    Decode();
    return &*decoded_packet_;
  }
  const DecodedTracePacket& operator*() { return *(operator->()); }

 private:
  TracePacket(const TracePacket&) = delete;
  TracePacket& operator=(const TracePacket&) = delete;

  const void* start_;
  size_t size_;
  std::unique_ptr<DecodedTracePacket> decoded_packet_;
};

}  // namespace perfetto

#endif  // TRACING_INCLUDE_TRACING_TRACE_PACKET_H_
