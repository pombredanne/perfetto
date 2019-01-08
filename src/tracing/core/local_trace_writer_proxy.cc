/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "perfetto/tracing/core/local_trace_writer_proxy.h"

#include "perfetto/base/logging.h"
#include "perfetto/trace/trace_packet.pbzero.h"
#include "perfetto/tracing/core/shared_memory_abi.h"
#include "src/tracing/core/patch_list.h"
#include "src/tracing/core/shared_memory_arbiter_impl.h"

using ChunkHeader = perfetto::SharedMemoryABI::ChunkHeader;

namespace perfetto {

namespace {

SharedMemoryABI::Chunk NewChunk(SharedMemoryArbiterImpl* arbiter,
                                WriterID writer_id,
                                ChunkID chunk_id,
                                bool fragmenting_packet) {
  ChunkHeader::Packets packets = {};
  if (fragmenting_packet) {
    packets.count = 1;
    packets.flags = ChunkHeader::kFirstPacketContinuesFromPrevChunk;
  }

  // The memory order of the stores below doesn't really matter. This |header|
  // is just a local temporary object. The GetNewChunk() call below will copy it
  // into the shared buffer with the proper barriers.
  ChunkHeader header = {};
  header.writer_id.store(writer_id, std::memory_order_relaxed);
  header.chunk_id.store(chunk_id, std::memory_order_relaxed);
  header.packets.store(packets, std::memory_order_relaxed);

  return arbiter->GetNewChunk(header);
}

class LocalBufferReader {
 public:
  LocalBufferReader(protozero::ScatteredHeapBuffer* buffer)
      : buffer_slices_(buffer->slices()), cur_slice_(buffer_slices_.begin()) {}

  size_t ReadBytes(SharedMemoryABI::Chunk* target_chunk, size_t num_bytes) {
    PERFETTO_CHECK(target_chunk->payload_size() >= num_bytes);
    uint8_t* chunk_payload = target_chunk->payload_begin();
    size_t bytes_read = 0;
    while (bytes_read < num_bytes) {
      if (cur_slice_ == buffer_slices_.end())
        return bytes_read;

      auto cur_slice_range = cur_slice_->GetUsedRange();

      if (cur_slice_range.size() == cur_slice_offset_) {
        cur_slice_offset_ = 0;
        cur_slice_++;
        continue;
      }

      size_t read_size = std::min(num_bytes - bytes_read,
                                  cur_slice_range.size() - cur_slice_offset_);
      memcpy(chunk_payload + bytes_read,
             cur_slice_range.begin + cur_slice_offset_, read_size);
      cur_slice_offset_ += read_size;
      bytes_read += read_size;

      // Should have either read all of the chunk or completed reading now.
      PERFETTO_DCHECK(cur_slice_offset_ == cur_slice_range.size() ||
                      bytes_read == num_bytes);
    }
    return bytes_read;
  }

 private:
  const std::vector<protozero::ScatteredHeapBuffer::Slice>& buffer_slices_;

  // Iterator pointing to slice in |buffer_slices_| that we're currently reading
  // from.
  std::vector<protozero::ScatteredHeapBuffer::Slice>::const_iterator cur_slice_;
  // Read offset in the current slice in bytes.
  size_t cur_slice_offset_ = 0;
};

}  // namespace

#if PERFETTO_DCHECK_IS_ON()
LocalTraceWriterProxy::ScopedLock::~ScopedLock() {
  if (proxy_) {
    PERFETTO_DCHECK(!proxy_->cur_packet_ ||
                    proxy_->cur_packet_->is_finalized());
  }
}
#endif  // PERFETTO_DCHECK_IS_ON()

LocalTraceWriterProxy::LocalTraceWriterProxy()
    : memory_buffer_(new protozero::ScatteredHeapBuffer()),
      memory_stream_writer_(
          new protozero::ScatteredStreamWriter(memory_buffer_.get())) {
  memory_buffer_->set_writer(memory_stream_writer_.get());
  PERFETTO_DETACH_FROM_THREAD(writer_thread_checker_);
}

LocalTraceWriterProxy::LocalTraceWriterProxy(
    std::unique_ptr<TraceWriter> trace_writer)
    : was_bound_(true), trace_writer_(std::move(trace_writer)) {}

LocalTraceWriterProxy::~LocalTraceWriterProxy() = default;

void LocalTraceWriterProxy::BindToTraceWriter(
    SharedMemoryArbiterImpl* arbiter,
    std::unique_ptr<TraceWriter> trace_writer,
    BufferID target_buffer) {
  std::lock_guard<std::mutex> lock(lock_);

  PERFETTO_DCHECK(!trace_writer_);

  // If there's a pending trace packet, it should have been completed by the
  // writer thread before releasing the lock.
  if (cur_packet_) {
    TracePacketCompleted();
    cur_packet_.reset();
  }
  memory_stream_writer_.reset();

  ChunkID next_chunk_id = CommitLocalBufferChunks(
      arbiter, trace_writer->writer_id(), target_buffer);
  memory_buffer_.reset();

  // The real TraceWriter should start writing at the subsequent chunk ID.
  trace_writer->SetFirstChunkId(next_chunk_id);

  trace_writer_ = std::move(trace_writer);
}

TraceWriter::TracePacketHandle LocalTraceWriterProxy::NewTracePacket() {
  PERFETTO_DCHECK_THREAD(writer_thread_checker_);
  if (trace_writer_)
    return trace_writer_->NewTracePacket();
  // Producer should have acquired lock already by calling BeginWrite().
  PERFETTO_DCHECK(!lock_.try_lock());
  if (cur_packet_) {
    TracePacketCompleted();
  } else {
    cur_packet_.reset(new protos::pbzero::TracePacket());
  }
  cur_packet_->Reset(memory_stream_writer_.get());
  return TraceWriter::TracePacketHandle(cur_packet_.get());
}

void LocalTraceWriterProxy::Flush(std::function<void()> callback) {
  PERFETTO_DCHECK_THREAD(writer_thread_checker_);
  if (trace_writer_)
    return trace_writer_->Flush(std::move(callback));
  // Can't flush while unbound.
  callback();
}

WriterID LocalTraceWriterProxy::writer_id() const {
  PERFETTO_DCHECK_THREAD(writer_thread_checker_);
  if (trace_writer_)
    return trace_writer_->writer_id();
  return 0;
}

void LocalTraceWriterProxy::TracePacketCompleted() {
  // If we hit this, the caller is calling NewTracePacket() or releasing the
  // lock without having finalized the previous packet.
  PERFETTO_DCHECK(cur_packet_->is_finalized());
  uint32_t packet_size = cur_packet_->Finalize();
  packet_sizes_.push_back(packet_size);
  total_payload_size += packet_size;
}

ChunkID LocalTraceWriterProxy::CommitLocalBufferChunks(
    SharedMemoryArbiterImpl* arbiter,
    WriterID writer_id,
    BufferID target_buffer) {
  // TODO(eseckler): Write and commit these chunks asynchronously. This would
  // require that the service is informed of the missing initial chunks, e.g. by
  // committing our first chunk here before the new trace writer has a chance to
  // commit its first chunk. Otherwise the service wouldn't know to wait for our
  // chunks.

  if (packet_sizes_.empty() || !writer_id)
    return 0;

  LocalBufferReader local_buffer_reader(memory_buffer_.get());

  ChunkID next_chunk_id = 0;
  SharedMemoryABI::Chunk cur_chunk =
      NewChunk(arbiter, writer_id, next_chunk_id++, false);

  size_t max_payload_size = cur_chunk.payload_size();
  size_t cur_payload_size = 0;
  uint16_t cur_num_packets = 0;
  size_t total_num_packets = packet_sizes_.size();
  PatchList patch_list;
  for (size_t packet_idx = 0; packet_idx < total_num_packets; packet_idx++) {
    uint32_t packet_size = packet_sizes_[packet_idx];
    uint32_t remaining_packet_size = packet_size;
    ++cur_num_packets;
    while (remaining_packet_size > 0) {
      uint32_t fragment_size = static_cast<uint32_t>(
          std::min(static_cast<size_t>(remaining_packet_size),
                   max_payload_size - cur_payload_size));
      cur_payload_size += fragment_size;
      remaining_packet_size -= fragment_size;

      bool last_write =
          packet_idx == total_num_packets - 1 && remaining_packet_size == 0;

      // Find num_packets that we should copy into current chunk and their
      // payload_size.
      bool write_chunk = cur_num_packets == ChunkHeader::Packets::kMaxCount ||
                         cur_payload_size == max_payload_size || last_write;

      if (write_chunk) {
        // Write chunk payload.
        local_buffer_reader.ReadBytes(&cur_chunk, cur_payload_size);

        cur_chunk.SetPacketCount(cur_num_packets);

        bool is_fragmenting = remaining_packet_size > 0;
        if (is_fragmenting)
          cur_chunk.SetFlag(ChunkHeader::kLastPacketContinuesOnNextChunk);

        arbiter->ReturnCompletedChunk(std::move(cur_chunk), target_buffer,
                                      &patch_list);

        // Avoid creating a new chunk after the last write.
        if (!last_write) {
          cur_chunk =
              NewChunk(arbiter, writer_id, next_chunk_id++, is_fragmenting);
          max_payload_size = cur_chunk.payload_size();
          cur_payload_size = 0;
          cur_num_packets = is_fragmenting ? 1 : 0;
        } else {
          PERFETTO_DCHECK(!is_fragmenting);
        }
      }
    }
  }

  // The last chunk should have been returned.
  PERFETTO_DCHECK(!cur_chunk.is_valid());

  return next_chunk_id;
}

}  // namespace perfetto
