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

#ifndef INCLUDE_PERFETTO_TRACING_CORE_LOCAL_TRACE_WRITER_PROXY_H_
#define INCLUDE_PERFETTO_TRACING_CORE_LOCAL_TRACE_WRITER_PROXY_H_

#include <memory>
#include <mutex>
#include <vector>

#include "perfetto/base/export.h"
#include "perfetto/base/optional.h"
#include "perfetto/base/thread_checker.h"
#include "perfetto/protozero/message_handle.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/protozero/scattered_stream_writer.h"
#include "perfetto/tracing/core/basic_types.h"
#include "perfetto/tracing/core/trace_writer.h"

namespace perfetto {

class SharedMemoryArbiterImpl;

namespace protos {
namespace pbzero {
class TracePacket;
}  // namespace pbzero
}  // namespace protos

// Facilitates writing trace events in early phases of an application's startup
// when the perfetto service is not available yet.
//
// Until the service is available, producer threads instantiate an unbound
// LocalTraceWriterProxy instance and use it to emit trace events. Each proxy
// will record the serialized trace events into a temporary local memory buffer.
//
// Once the service is available, the producer binds each proxy to a TraceWriter
// backed by the SMB by calling SharedMemoryArbiter::CreateProxiedTraceWriter().
// The data in the proxy's local buffer will then be copied into the SMB and the
// any future writes will proxy directly to the new SMB-backed TraceWriter.
//
// Writing to the temporary local trace buffer is guarded by a lock to allow
// binding the proxy from a different thread. Before writing, the writer thread
// needs to acquire a scoped lock by calling BeginWrite(). Before releasing this
// lock, it has to finalize the TracePacket is was writing.
class PERFETTO_EXPORT LocalTraceWriterProxy : public TraceWriter {
 public:
  // Create an unbound proxy that can later be bound by calling
  // BindToTraceWriter().
  LocalTraceWriterProxy();
  // Create a proxy bound to |trace_writer|. Should only be called on the writer
  // thread.
  LocalTraceWriterProxy(std::unique_ptr<TraceWriter> trace_writer);

  ~LocalTraceWriterProxy() override;

#if PERFETTO_DCHECK_IS_ON()
  // Verify that the last TracePacket was finalized before release of the write
  // lock in debug builds.
  class ScopedLock {
   public:
    ScopedLock() = default;
    ScopedLock(LocalTraceWriterProxy* proxy, std::mutex& mutex)
        : proxy_(proxy), lock_(mutex) {}
    ScopedLock(ScopedLock&&) = default;
    ~ScopedLock();

   private:
    LocalTraceWriterProxy* proxy_ = nullptr;
    std::unique_lock<std::mutex> lock_;
  };
#else  // PERFETTO_DCHECK_IS_ON()
  using ScopedLock = std::unique_lock<std::mutex>;
#endif  // PERFETTO_DCHECK_IS_ON()

  // Called by the writer thread when it needs to emit data. The returned
  // std::unique_lock needs to be held for the duration of the writing activity.
  // When released, the written TracePacket needs to be finalized.
  //
  // If the proxy is still unbound, this acquires the proxy's lock. Otherwise,
  // it will try to avoid locking and return an empty std::unique_lock, since
  // writing directly to the bound TraceWriter is thread-safe.
  inline ScopedLock BeginWrite() {
    PERFETTO_DCHECK_THREAD(writer_thread_checker_);

    // Check if we are already bound without grabbing the lock. This is an
    // optimization to avoid any locking in the common case where the proxy was
    // bound some time ago.
    if (was_bound_)
      return ScopedLock();

// Now grab the lock and safely check whether we are still unbound. If
// unbound, we return the lock. Otherwise, we release it again (because the
// proxy was concurrently bound and thus no locking is necessary anymore).
#if PERFETTO_DCHECK_IS_ON()
    ScopedLock lock(this, lock_);
#else   // PERFETTO_DCHECK_IS_ON()
    ScopedLock lock(lock_);
#endif  // PERFETTO_DCHECK_IS_ON()
    if (trace_writer_) {
      was_bound_ = true;
      return ScopedLock();
    }
    return lock;
  }

  // TraceWriter implementation. These methods should only be called on the
  // writer thread and while the lock returned by BeginWrite is held.
  TracePacketHandle NewTracePacket() override;
  void Flush(std::function<void()> callback = {}) override;
  WriterID writer_id() const override;

  // Bind this proxy to the provided TraceWriter and SharedMemoryArbiterImpl.
  // Called by SharedMemoryArbiterImpl::CreateProxiedTraceWriter().
  //
  // This method can be called on any thread. If any data was written locally
  // before the proxy was bound, it will copy this data into chunks in the
  // TraceWriter's target buffer via the SMB. The TraceWriter will be instructed
  // to begin its future writes with the ChunkID following the last one written
  // by the proxy.
  void BindToTraceWriter(SharedMemoryArbiterImpl*,
                         std::unique_ptr<TraceWriter>,
                         BufferID target_buffer);

  size_t used_buffer_size() const {
    size_t used_size = 0;
    memory_buffer_->AdjustUsedSizeOfCurrentSlice();
    for (const auto& slice : memory_buffer_->slices()) {
      used_size += slice.GetUsedRange().size();
    }
    return used_size;
  }

 private:
  void TracePacketCompleted();
  ChunkID CommitLocalBufferChunks(SharedMemoryArbiterImpl*, WriterID, BufferID);

  PERFETTO_THREAD_CHECKER(writer_thread_checker_)

  // Only set and accessed from the writer thread. The writer thread flips this
  // bit when it sees that trace_writer_ is set (while holding the lock).
  // Caching this fact in this variable avoids the need to acquire the lock to
  // check on later calls to BeginWrite().
  bool was_bound_ = false;

  // All variables below this point are protected by |lock_|.
  std::mutex lock_;

  // Never reset once it is changed from |nullptr|.
  std::unique_ptr<TraceWriter> trace_writer_ = nullptr;

  // Local memory buffer for trace packets written before the proxy is bound.
  std::unique_ptr<protozero::ScatteredHeapBuffer> memory_buffer_;
  std::unique_ptr<protozero::ScatteredStreamWriter> memory_stream_writer_;

  std::vector<uint32_t> packet_sizes_;
  size_t total_payload_size = 0;

  // The packet returned via NewTracePacket() while the proxy is unbound. Its
  // owned by this class, TracePacketHandle has just a pointer to it.
  std::unique_ptr<protos::pbzero::TracePacket> cur_packet_;
};

}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACING_CORE_LOCAL_TRACE_WRITER_PROXY_H_
