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

#include "src/trace_processor/trace_parser.h"

#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"
#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"

namespace perfetto {
namespace trace_processor {

using protozero::ProtoDecoder;
using protozero::proto_utils::kFieldTypeLengthDelimited;

namespace {
uint64_t FindIntField(ProtoDecoder* decoder, uint32_t field_id) {
  uint64_t value = std::numeric_limits<uint64_t>::max();
  for (auto f = decoder->ReadField(); f.id != 0; f = decoder->ReadField()) {
    if (f.id == field_id) {
      value = f.int_value;
      break;
    }
  }
  return value;
}
}  // namespace

TraceParser::TraceParser(BlobReader* reader,
                         TraceStorage* trace,
                         uint32_t chunk_size_b)
    : reader_(reader), trace_(trace), chunk_size_b_(chunk_size_b) {}

void TraceParser::ParseNextChunk() {
  if (!buffer_)
    buffer_.reset(new uint8_t[chunk_size_b_]);

  uint32_t read = reader_->Read(offset_, chunk_size_b_, buffer_.get());
  if (read == 0)
    return;

  ProtoDecoder decoder(buffer_.get(), read);
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    if (fld.id != protos::Trace::kPacketFieldNumber) {
      PERFETTO_ELOG("Non-trace packet field found in root Trace proto");
      continue;
    }
    ParsePacket(fld.length_value.data, fld.length_value.length);
  }

  offset_ += read;
}

void TraceParser::ParsePacket(const uint8_t* data, uint64_t length) {
  ProtoDecoder decoder(data, length);
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::TracePacket::kFtraceEventsFieldNumber:
        ParseFtraceEventBundle(fld.length_value.data, fld.length_value.length);
        break;
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void TraceParser::ParseFtraceEventBundle(const uint8_t* data, uint64_t length) {
  ProtoDecoder decoder(data, length);
  uint64_t cpu =
      FindIntField(&decoder, protos::FtraceEventBundle::kCpuFieldNumber);
  if (cpu == std::numeric_limits<uint64_t>::max()) {
    PERFETTO_ELOG("CPU field not found in FtraceEventBundle");
    return;
  }
  decoder.Reset();

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::FtraceEventBundle::kEventFieldNumber:
        ParseFtraceEvent(static_cast<uint32_t>(cpu), fld.length_value.data,
                         fld.length_value.length);
        break;
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void TraceParser::ParseFtraceEvent(uint32_t cpu,
                                   const uint8_t* data,
                                   uint64_t length) {
  ProtoDecoder decoder(data, length);
  uint64_t timestamp =
      FindIntField(&decoder, protos::FtraceEvent::kTimestampFieldNumber);
  if (timestamp == std::numeric_limits<uint64_t>::max()) {
    PERFETTO_ELOG("Timestamp field not found in FtraceEvent");
    return;
  }
  decoder.Reset();

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::FtraceEvent::kSchedSwitchFieldNumber:
        PERFETTO_DCHECK(timestamp > 0);
        ParseSchedSwitch(cpu, timestamp, fld.length_value.data,
                         fld.length_value.length);
        break;
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void TraceParser::ParseSchedSwitch(uint32_t cpu,
                                   uint64_t timestamp,
                                   const uint8_t* data,
                                   uint64_t length) {
  ProtoDecoder decoder(data, length);

  uint32_t prev_pid = 0;
  uint32_t next_pid = 0;
  std::string next_comm;
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::SchedSwitchFtraceEvent::kPrevPidFieldNumber:
        prev_pid = static_cast<uint32_t>(fld.int_value);
        break;
      case protos::SchedSwitchFtraceEvent::kNextPidFieldNumber:
        next_pid = static_cast<uint32_t>(fld.int_value);
        break;
      case protos::SchedSwitchFtraceEvent::kNextCommFieldNumber:
        next_comm =
            std::string(reinterpret_cast<const char*>(fld.length_value.data),
                        fld.length_value.length);
        break;
      default:
        break;
    }
  }

  // TODO(lalitm): store these fields inside the TraceStorage class.
  perfetto::base::ignore_result(cpu);
  perfetto::base::ignore_result(timestamp);
  perfetto::base::ignore_result(trace_);

  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

}  // namespace trace_processor
}  // namespace perfetto
