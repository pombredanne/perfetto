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

#include "src/trace_processor/proto_trace_tokenizer.h"

#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"
#include "perfetto/protozero/proto_decoder.h"
#include "src/trace_processor/blob_reader.h"
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/sched_tracker.h"
#include "src/trace_processor/trace_processor_context.h"

#include "src/trace_processor/trace_sorter.h"

#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"

namespace perfetto {
namespace trace_processor {

using protozero::ProtoDecoder;
using protozero::proto_utils::kFieldTypeLengthDelimited;

namespace {

bool FindIntField(ProtoDecoder* decoder,
                  uint32_t field_id,
                  uint64_t* field_value) {
  for (auto f = decoder->ReadField(); f.id != 0; f = decoder->ReadField()) {
    if (f.id == field_id) {
      *field_value = f.int_value;
      return true;
    }
  }
  return false;
}
}  // namespace

ProtoTraceTokenizer::ProtoTraceTokenizer(BlobReader* reader,
                                         TraceProcessorContext* context)
    : reader_(reader), context_(context) {}

ProtoTraceTokenizer::~ProtoTraceTokenizer() = default;

bool ProtoTraceTokenizer::ParseNextChunk() {
  std::shared_ptr<uint8_t> buffer(new uint8_t[chunk_size_],
                                  std::default_delete<uint8_t[]>());

  uint32_t read = reader_->Read(offset_, chunk_size_, buffer.get());
  if (read == 0)
    return false;

  ProtoDecoder decoder(buffer.get(), read);
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    if (fld.id != protos::Trace::kPacketFieldNumber) {
      continue;
    }
    uint64_t offset = static_cast<uint64_t>(fld.data() - buffer.get());
    TraceBlobView packet_view = {buffer, offset, fld.size()};
    ParsePacket(packet_view);
  }

  offset_ += decoder.offset();
  return true;
}

void ProtoTraceTokenizer::ParsePacket(const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  // TODO(taylori): Add a timestamp to TracePacket and read it here.

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::TracePacket::kFtraceEventsFieldNumber: {
        uint64_t offset = static_cast<uint64_t>(fld.data() - view.buffer.get());
        TraceBlobView ftrace_view = {view.buffer, offset, fld.size()};
        ParseFtraceEventBundle(ftrace_view);
        break;
      }
      default: {
        // Use parent data and length because we want to parse this again
        // later to get the exact type of the packet.
        context_->sorter->PushTracePacket(last_timestamp + 1, view);
        break;
      }
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceTokenizer::ParseFtraceEventBundle(const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  uint64_t cpu = 0;
  if (!FindIntField(&decoder, protos::FtraceEventBundle::kCpuFieldNumber,
                    &cpu)) {
    PERFETTO_ELOG("CPU field not found in FtraceEventBundle");
    return;
  }
  decoder.Reset();

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::FtraceEventBundle::kEventFieldNumber: {
        uint64_t offset = static_cast<uint64_t>(fld.data() - view.buffer.get());
        TraceBlobView ftrace_view = {view.buffer, offset, fld.size()};
        ParseFtraceEvent(static_cast<uint32_t>(cpu), ftrace_view);
        break;
      }
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceTokenizer::ParseFtraceEvent(uint32_t cpu,
                                           const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  uint64_t timestamp = 0;
  if (!FindIntField(&decoder, protos::FtraceEvent::kTimestampFieldNumber,
                    &timestamp)) {
    PERFETTO_ELOG("Timestamp field not found in FtraceEvent");
    return;
  }
  decoder.Reset();

  // We don't need to parse this packet, just push it to be sorted with
  // the timestamp.
  context_->sorter->PushFtracePacket(cpu, timestamp, view);
}

}  // namespace trace_processor
}  // namespace perfetto
