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

ProtoTraceTokenizer::ProtoTraceTokenizer(BlobReader* reader,
                                         TraceProcessorContext* context)
    : reader_(reader), context_(context) {}

ProtoTraceTokenizer::~ProtoTraceTokenizer() = default;

bool ProtoTraceTokenizer::ParseNextChunk() {
  std::shared_ptr<uint8_t> shared_buf(new uint8_t[chunk_size_],
                                      std::default_delete<uint8_t[]>());
  uint8_t* buf = shared_buf.get();

  uint32_t read = reader_->Read(offset_, chunk_size_, buf);
  if (read == 0)
    return false;

  ProtoDecoder decoder(buf, read);
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    if (fld.id != protos::Trace::kPacketFieldNumber) {
      PERFETTO_ELOG("Non-trace packet field found in root Trace proto");
      continue;
    }
    size_t offset = static_cast<size_t>(fld.data() - buf);
    TraceBlobView packet_view(shared_buf, offset, fld.size());
    ParsePacket(std::move(packet_view));
  }

  if (decoder.offset() == 0) {
    PERFETTO_ELOG("The trace file seems truncated, interrupting parsing");
    return false;
  }

  offset_ += decoder.offset();
  return true;
}

void ProtoTraceTokenizer::ParsePacket(TraceBlobView view) {
  ProtoDecoder decoder(view.data(), view.length());

  // TODO(taylori): Add a timestamp to TracePacket and read it here.

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::TracePacket::kFtraceEventsFieldNumber: {
        TraceBlobView ftrace_view(view.buffer(), view.offset_of(fld.data()),
                                  fld.size());
        ParseFtraceEventBundle(ftrace_view);
        break;
      }
      default: {
        // Use parent data and length because we want to parse this again
        // later to get the exact type of the packet.
        context_->sorter->PushTracePacket(last_timestamp + 1, std::move(view));
        break;
      }
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceTokenizer::ParseFtraceEventBundle(const TraceBlobView& view) {
  ProtoDecoder decoder(view.data(), view.length());

  auto field = decoder.FindIntField(protos::FtraceEventBundle::kCpuFieldNumber);
  if (field.id == 0) {
    PERFETTO_ELOG("CPU field not found in FtraceEventBundle");
    return;
  }
  uint64_t cpu = field.int_value;

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::FtraceEventBundle::kEventFieldNumber: {
        TraceBlobView ftrace_view(view.buffer(), view.offset_of(fld.data()),
                                  fld.size());
        ParseFtraceEvent(static_cast<uint32_t>(cpu), std::move(ftrace_view));
        break;
      }
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceTokenizer::ParseFtraceEvent(uint32_t cpu, TraceBlobView view) {
  ProtoDecoder decoder(view.data(), view.length());

  auto field = decoder.FindIntField(protos::FtraceEvent::kTimestampFieldNumber);
  if (field.id == 0) {
    PERFETTO_ELOG("Timestamp field not found in FtraceEvent");
    return;
  }
  uint64_t timestamp = field.int_value;

  // We don't need to parse this packet, just push it to be sorted with
  // the timestamp.
  context_->sorter->PushFtracePacket(cpu, timestamp, std::move(view));
}

}  // namespace trace_processor
}  // namespace perfetto
