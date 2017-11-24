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

#ifndef TRACING_SRC_CORE_TRACE_WRITER_H
#define TRACING_SRC_CORE_TRACE_WRITER_H

#include "protozero/protozero_message_handle.h"

namespace perfetto {

namespace protos {
class TracePacket;
}  // namespace protos

// This is a single-thread write interface that allows to write protobufs
// directly into the tracing shared buffer without making any copies.
// It takes care of acquiring and releasing chunks from the
// ProducerSharedMemoryArbiter and splitting protos over chunks.
// The idea is that each data source creates one TraceWriter for each thread
// it wants to write from. Each TraceWriter will get its own dedicated chunk
// and will write into the shared buffer without any locking most of the time.
// Locking will happen only when a chunk is exhausted and a new one is acquired
// from the arbiter.

// TODO: TraceWriter needs to keep the shared memory buffer alive (refcount?).
// If the shared memory buffer goes away (e.g. the Service crashes) the
// TraceWriter here will happily keep writing into unmapped memory.

class TraceWriter {
 public:
  using TracePacketHandle =
      protozero::ProtoZeroMessageHandle<protos::TracePacket>;

  TraceWriter();
  virtual ~TraceWriter();

  virtual TracePacketHandle NewTracePacket() = 0;

 private:
  TraceWriter(const TraceWriter&) = delete;
  TraceWriter& operator=(const TraceWriter&) = delete;
};

}  // namespace perfetto

#endif  // TRACING_SRC_CORE_TRACE_WRITER_H
