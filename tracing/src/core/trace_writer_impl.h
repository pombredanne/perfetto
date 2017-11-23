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

#ifndef TRACING_SRC_CORE_TRACE_WRITER_IMPL_H
#define TRACING_SRC_CORE_TRACE_WRITER_IMPL_H

#include <functional>

#include "protozero/protozero_message_handle.h"
#include "protozero/scattered_stream_writer.h"
#include "tracing/core/basic_types.h"
#include "tracing/core/shared_memory_abi.h"
#include "tracing/core/trace_writer.h"

#include "protos/trace_packet.pbzero.h"

namespace perfetto {

class ProducerSharedMemoryArbiter;

// See include/core/trace_writer.h for docs.
class TraceWriterImpl : public TraceWriter,
                        public protozero::ScatteredStreamWriter::Delegate {
 public:
  using TracePacket = pbzero::TracePacket;
  using TracePacketHandle = protozero::ProtoZeroMessageHandle<TracePacket>;

  TraceWriterImpl(ProducerSharedMemoryArbiter*, WriterID);
  ~TraceWriterImpl() override;

  // TraceWriter implementation.
  TracePacketHandle NewTracePacket() override;

 private:
  TraceWriterImpl(const TraceWriterImpl&) = delete;
  TraceWriterImpl& operator=(const TraceWriterImpl&) = delete;

  // Called when a TracePacketHandle goes out of scope (or the ProtoZeroMessage
  // is explicitly Finalize()-d).

  void OnFinalize(size_t packet_size);

  // ScatteredStreamWriter::Delegate implementation.
  protozero::ContiguousMemoryRange GetNewBuffer() override;

  // The per-producer arbiter that coordinates access to the shared memory
  // buffer from several threads.
  ProducerSharedMemoryArbiter* const shmem_arbiter_;

  // ID of the current writer.
  const WriterID id_;

  // Monotonic sequence id of the chunk. Together with the WriterID  is allows
  // the Service to to reconstruct the linear sequence of packets.
  uint16_t cur_chunk_id_ = 0;

  // The chunk we are holding onto (if any).
  SharedMemoryABI::Chunk cur_chunk_;

  // It is passed to protozero message to write directly onto |cur_chunk_|. It
  // keeps track of the write pointer. It calls us back (GetNewBuffer()) when
  // |cur_chunk_| is exhausted.
  protozero::ScatteredStreamWriter protobuf_stream_writer_;

  // The packet returned via NewTracePacket(). Its owned by us,
  // TracePacketHandle has just a pointer to it.
  TracePacket cur_packet_;

  // The start address, within |cur_cunk_| bounds, of |cur_packet_|. Used to
  // figure out frament sizes when a TracePacket write is interrupted by
  // GetNewBuffer().
  uintptr_t cur_packet_start_ = 0;

  // true if we received a call to NewTracePacket() and the caller has not
  // finalized/destroyed the returned handle (i.e. the caller is still writing
  // on the TracePacket).
  bool cur_packet_being_written_ = false;

  // Points to the 2 bytes packet header that tells the size of the packet
  // (fragment) within the chunk.
  uintptr_t cur_packet_header_ = 0;

  std::function<void(size_t)> finalize_callback_;
};

}  // namespace perfetto

#endif  // TRACING_SRC_CORE_TRACE_WRITER_IMPL_H
