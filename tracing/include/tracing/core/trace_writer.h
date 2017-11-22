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

#ifndef TRACING_INCLUDE_TRACING_CORE_SERVICE_H_
#define TRACING_INCLUDE_TRACING_CORE_SERVICE_H_

#include <functional>

#include "protozero/protozero_message_handle.h"
#include "protozero/scattered_stream_writer.h"
#include "tracing/core/basic_types.h"
#include "tracing/core/shared_memory_abi.h"

#include "protos/trace_packet.pbzero.h"

namespace perfetto {

class ProducerSharedMemoryArbiter;

class TraceWriter : public protozero::ScatteredStreamWriter::Delegate {
 public:
  using TracePacket = pbzero::TracePacket;
  using TracePacketHandle = protozero::ProtoZeroMessageHandle<TracePacket>;

  explicit TraceWriter(ProducerSharedMemoryArbiter*);
  TracePacketHandle NewTracePacket();

  // ScatteredStreamWriter::Delegate implementation.
  protozero::ContiguousMemoryRange GetNewBuffer() override;


 private:
   TraceWriter(const TraceWriter&) = delete;
   TraceWriter& operator=(const TraceWriter&) = delete;

   void OnFinalize(size_t packet_size);

   ProducerSharedMemoryArbiter* const shmem_arbiter_;
   const WriterID id_;
   SharedMemoryABI::Chunk cur_chunk_;
   uint16_t cur_chunk_id_ = 0;
   protozero::ScatteredStreamWriter protobuf_stream_writer_;
   TracePacket cur_packet_;
   uintptr_t cur_packet_start_ = 0;
   size_t cur_packet_prev_fragments_size_ = 0;
   bool cur_packet_being_written_ = false;
   protozero::ContiguousMemoryRange cur_packet_header_ = {};
   std::function<void(size_t)> finalize_callback_;
};

}  // namespace perfetto

#endif  // TRACING_INCLUDE_TRACING_CORE_SERVICE_H_
