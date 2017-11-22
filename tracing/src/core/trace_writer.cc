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

#include "tracing/core/trace_writer.h"

#include <string.h>

#include <type_traits>
#include <utility>

#include "tracing/core/shared_memory_abi.h"
#include "tracing/src/core/producer_shared_memory_arbiter.h"

#include "base/logging.h"

namespace perfetto {

namespace {
constexpr size_t kPacketHeaderSize = SharedMemoryABI::kPacketHeaderSize;
}  // namespace

using PacketHeaderType = SharedMemoryABI::PacketHeaderType;

// TODO: we should figure out a way to ensure that the caller doesn't keep
// a TracePacket alive after the TraceWriter (or the full shared memory buffer)
// have been destroyed. There are two problems here:
// 1. |finalize_callback_| should be dropped if TraceWriter goes away (could
// do with a WeakPtr, but a less invasive pattern would be nicer).
// 2. The underlying shared memory buffer goes away. At that point even if
// the TraceWriter is alive, the TracePacket would write onto unmapped memory.
// I think the right solution is to make the shared memory buffer refcounted and
// guarantee that it can't go away if there if any TracePacket is alive.
// Temporarily the shared memory buffer is long lived and this is not a problem.

TraceWriter::TraceWriter(ProducerSharedMemoryArbiter* shmem_arbiter)
    : shmem_arbiter_(shmem_arbiter),
      id_(shmem_arbiter->AcquireWriterID()),
      protobuf_stream_writer_(this) {
  finalize_callback_ = [this](size_t packet_size) { OnFinalize(packet_size); };

  // TODO we could handle this more gracefully and always return some garbage
  // TracePacket in NewTracePacket.
  PERFETTO_CHECK(id_ == 0);
}

TraceWriter::TracePacketHandle TraceWriter::NewTracePacket() {
  // If we hit this, the caller is calling NewTracePacket() without having
  // finalized the previous packet.
  PERFETTO_DCHECK(!cur_packet_being_written_);

  // Reserve space for the size of the message.
  cur_packet_header_ = protobuf_stream_writer_.ReserveBytes(kPacketHeaderSize);
  memset(cur_packet_header_.begin, 0, kPacketHeaderSize);
  cur_packet_.Reset(&protobuf_stream_writer_);
  TracePacketHandle handle(&cur_packet_);
  handle.set_on_finalize(finalize_callback_);
  cur_packet_being_written_ = true;
  cur_packet_prev_fragments_size_ = 0;
  cur_packet_start_ =
      reinterpret_cast<uintptr_t>(protobuf_stream_writer_.write_ptr());
  return handle;
}

void TraceWriter::OnFinalize(size_t packet_size) {
  PERFETTO_CHECK(cur_packet_being_written_);
  PERFETTO_DCHECK(cur_packet_header_.is_valid());
  PERFETTO_DCHECK(cur_packet_header_.size() == kPacketHeaderSize);

  // This could be the OnFinalize() call for a packet o that was startd in
  // a previous chunk and is continuing in the current one. In this case
  // |packet_size| will report the full size of the packet, without taking
  // into account any fragmentation due to to chunks. However, we want to write
  // only the size of the fragment that lays in the current chunk.

  PERFETTO_DCHECK(cur_packet_prev_fragments_size_ == 0 ||
                  cur_chunk_.GetPacketCount() == 1);

  size_t size_in_cur_chunk = packet_size - cur_packet_prev_fragments_size_;
  PERFETTO_DCHECK(size_in_cur_chunk <= 1 << (8 * kPacketHeaderSize));
  const PacketHeaderType size = static_cast<PacketHeaderType>(packet_size);
  memcpy(cur_packet_header_.begin, &size, sizeof(size));
  cur_packet_being_written_ = false;

  // Keep this last, it has release-store semantics.
  cur_chunk_.IncreasePacketCount();
}

// Called by the ProtoZeroMessage when it runs out of space.
protozero::ContiguousMemoryRange TraceWriter::GetNewBuffer() {
  bool last_packet_is_partial = false;
  if (cur_packet_being_written_) {
    const uintptr_t wptr =
        reinterpret_cast<uintptr_t>(protobuf_stream_writer_.write_ptr());
    PERFETTO_DCHECK(wptr >= cur_packet_start_);
    size_t partial_packet_size = wptr - cur_packet_start_;
    PERFETTO_DCHECK(partial_packet_size < cur_chunk_.size());
    const auto size = static_cast<PacketHeaderType>(partial_packet_size);
    memcpy(cur_packet_header_.begin, &size, sizeof(size));
    last_packet_is_partial = true;
  }
  cur_chunk_.IncreasePacketCount(last_packet_is_partial);

  // Start a new chunk.
  SharedMemoryABI::ChunkHeader::Identifier identifier = {};
  static_assert(sizeof(id_) <= sizeof(identifier.writer_id),
                "WriterID too large");
  identifier.writer_id = id_;
  identifier.chunk_id = cur_chunk_id_++;

  SharedMemoryABI::ChunkHeader::PacketsState packets_state = {};
  if (cur_packet_being_written_) {
    packets_state.count = 1;
    packets_state.flags = SharedMemoryABI::ChunkHeader::PacketsState::
        kFirstPacketContinuesFromPrevChunk;
  }

  // The memory order of the stores below doesn't really matter. This |header|
  // is just a temporary object and GetNewChunk() will copy it into the shared
  // buffer with the proper barriers.
  SharedMemoryABI::ChunkHeader header = {};
  header.identifier.store(identifier, std::memory_order_relaxed);
  header.packets.store(packets_state, std::memory_order_relaxed);
  cur_chunk_ = shmem_arbiter_->GetNewChunk(header);
  // TODO reserve size unsafe for the new size.
  // //////////////////////////////////////
  cur_packet_start_ = reinterpret_cast<uintptr_t>(cur_chunk_.payload_begin());

  // TODO get rid of the uint8_t* cast below.
  return protozero::ContiguousMemoryRange{
      reinterpret_cast<uint8_t*>(cur_chunk_.payload_begin()), reinterpret_cast<uint8_t*>(cur_chunk_.end())};
}

}  // namespace perfetto
