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
#include "perfetto/protozero/proto_utils.h"
#include "src/trace_processor/event_tracker.h"
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/stats.h"
#include "src/trace_processor/trace_blob_view.h"
#include "src/trace_processor/trace_sorter.h"
#include "src/trace_processor/trace_storage.h"

#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"

#include "perfetto/trace/trace.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace trace_processor {

using protozero::ProtoDecoder;
using protozero::ProtoDecoder2;
using protozero::proto_utils::MakeTagLengthDelimited;
using protozero::proto_utils::MakeTagVarInt;
using protozero::proto_utils::ParseVarInt;

ProtoTraceTokenizer::ProtoTraceTokenizer(TraceProcessorContext* ctx)
    : trace_sorter_(ctx->sorter.get()), trace_storage_(ctx->storage.get()) {}
ProtoTraceTokenizer::~ProtoTraceTokenizer() = default;

bool ProtoTraceTokenizer::Parse(std::unique_ptr<uint8_t[]> owned_buf,
                                size_t size) {
  uint8_t* data = &owned_buf[0];
  if (!partial_buf_.empty()) {
    // It takes ~5 bytes for a proto preamble + the varint size.
    const size_t kHeaderBytes = 5;
    if (PERFETTO_UNLIKELY(partial_buf_.size() < kHeaderBytes)) {
      size_t missing_len = std::min(kHeaderBytes - partial_buf_.size(), size);
      partial_buf_.insert(partial_buf_.end(), &data[0], &data[missing_len]);
      if (partial_buf_.size() < kHeaderBytes)
        return true;
      data += missing_len;
      size -= missing_len;
    }

    // At this point we have enough data in partial_buf_ to read at least the
    // field header and know the size of the next TracePacket.
    constexpr uint8_t kTracePacketTag =
        MakeTagLengthDelimited(protos::Trace::kPacketFieldNumber);
    const uint8_t* pos = &partial_buf_[0];
    uint8_t proto_field_tag = *pos;
    uint64_t field_size = 0;
    const uint8_t* next = ParseVarInt(++pos, &*partial_buf_.end(), &field_size);
    bool parse_failed = next == pos;
    pos = next;
    if (proto_field_tag != kTracePacketTag || field_size == 0 || parse_failed) {
      PERFETTO_ELOG("Failed parsing a TracePacket from the partial buffer");
      return false;  // Unrecoverable error, stop parsing.
    }

    // At this point we know how big the TracePacket is.
    size_t hdr_size = static_cast<size_t>(pos - &partial_buf_[0]);
    size_t size_incl_header = static_cast<size_t>(field_size + hdr_size);
    PERFETTO_DCHECK(size_incl_header > partial_buf_.size());

    // There is a good chance that between the |partial_buf_| and the new |data|
    // of the current call we have enough bytes to parse a TracePacket.
    if (partial_buf_.size() + size >= size_incl_header) {
      // Create a new buffer for the whole TracePacket and copy into that:
      // 1) The beginning of the TracePacket (including the proto header) from
      //    the partial buffer.
      // 2) The rest of the TracePacket from the current |data| buffer (note
      //    that we might have consumed already a few bytes form |data| earlier
      //    in this function, hence we need to keep |off| into account).
      std::unique_ptr<uint8_t[]> buf(new uint8_t[size_incl_header]);
      memcpy(&buf[0], partial_buf_.data(), partial_buf_.size());
      // |size_missing| is the number of bytes for the rest of the TracePacket
      // in |data|.
      size_t size_missing = size_incl_header - partial_buf_.size();
      memcpy(&buf[partial_buf_.size()], &data[0], size_missing);
      data += size_missing;
      size -= size_missing;
      partial_buf_.clear();
      uint8_t* buf_start = &buf[0];  // Note that buf is std::moved below.
      ParseInternal(std::move(buf), buf_start, size_incl_header);
    } else {
      partial_buf_.insert(partial_buf_.end(), data, &data[size]);
      return true;
    }
  }
  ParseInternal(std::move(owned_buf), data, size);
  return true;
}

void ProtoTraceTokenizer::ParseInternal(std::unique_ptr<uint8_t[]> owned_buf,
                                        uint8_t* data,
                                        size_t size) {
  PERFETTO_DCHECK(data >= &owned_buf[0]);
  const uint8_t* start = &owned_buf[0];
  const size_t data_off = static_cast<size_t>(data - start);
  TraceBlobView whole_buf(std::move(owned_buf), data_off, size);

  protos::pbzero::Trace::Parser decoder(data, size);
  for (auto it = decoder.packet(); it; ++it) {
    size_t field_offset2 = whole_buf.offset_of(it->data());
    ParsePacket(whole_buf.slice(field_offset2, it->size()));
  }

  const size_t bytes_left = decoder.bytes_left();
  if (bytes_left > 0) {
    PERFETTO_DCHECK(partial_buf_.empty());
    partial_buf_.insert(partial_buf_.end(), &data[decoder.read_offset()],
                        &data[decoder.read_offset() + bytes_left]);
  }
}

void ProtoTraceTokenizer::ParsePacket(TraceBlobView packet) {
  constexpr auto kTimestampFieldNumber =
      protos::TracePacket::kTimestampFieldNumber;
  ProtoDecoder2<kTimestampFieldNumber> decoder(packet.data(), packet.length());

  const auto& ts_field = decoder.Get<kTimestampFieldNumber>();
  auto timestamp = ts_field.valid() ? ts_field.as_int64() : latest_timestamp_;
  latest_timestamp_ = std::max(timestamp, latest_timestamp_);

  const auto& ftrace_field =
      decoder.Get<protos::TracePacket::kFtraceEventsFieldNumber>();
  if (ftrace_field.valid()) {
    const size_t fld_off = packet.offset_of(ftrace_field.data());
    ParseFtraceBundle(packet.slice(fld_off, ftrace_field.size()));
    return;
  }

  // Use parent data and length because we want to parse this again
  // later to get the exact type of the packet.
  trace_sorter_->PushTracePacket(timestamp, std::move(packet));
  PERFETTO_DCHECK(!decoder.bytes_left());
}

// PERFETTO_ALWAYS_INLINE
void ProtoTraceTokenizer::ParseFtraceBundle(TraceBlobView bundle) {
  const uint8_t* data = bundle.data();
  const size_t length = bundle.length();
  constexpr auto kEventFieldNumber =
      protos::FtraceEventBundle::kEventFieldNumber;
  ProtoDecoder2<kEventFieldNumber> decoder(data, length);

  const auto& cpu_field =
      decoder.Get<protos::FtraceEventBundle::kCpuFieldNumber>();

  if (PERFETTO_UNLIKELY(!cpu_field.valid())) {
    PERFETTO_ELOG("CPU field not found in FtraceEventBundle");
    trace_storage_->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return;
  }

  auto cpu = cpu_field.as_uint32();

  if (PERFETTO_UNLIKELY(cpu > base::kMaxCpus)) {
    PERFETTO_ELOG("CPU larger than kMaxCpus (%u > %zu)", cpu, base::kMaxCpus);
    return;
  }

  for (auto it = decoder.GetRepeated(kEventFieldNumber); it; ++it) {
    size_t off = bundle.offset_of(it->data());
    ParseFtraceEvent(cpu, bundle.slice(off, it->size()));
  }
  trace_sorter_->FinalizeFtraceEventBatch(static_cast<uint32_t>(cpu));
}

PERFETTO_ALWAYS_INLINE
void ProtoTraceTokenizer::ParseFtraceEvent(uint32_t cpu, TraceBlobView event) {
  constexpr auto kTimestampFieldNumber =
      protos::FtraceEvent::kTimestampFieldNumber;
  const uint8_t* data = event.data();
  const size_t length = event.length();
  ProtoDecoder decoder(data, length);
  uint64_t raw_timestamp = 0;
  bool timestamp_found = false;

  // Speculate on the fact that the timestamp is often the 1st field of the
  // event.
  constexpr auto timestampFieldTag = MakeTagVarInt(kTimestampFieldNumber);
  if (PERFETTO_LIKELY(length > 10 && data[0] == timestampFieldTag)) {
    // Fastpath.
    const uint8_t* next = ParseVarInt(data + 1, data + 11, &raw_timestamp);
    timestamp_found = next != data + 1;
    decoder.Reset(next);
  } else {
    // Slowpath.
    if (auto ts_field = decoder.FindField(kTimestampFieldNumber)) {
      timestamp_found = true;
      raw_timestamp = ts_field.as_uint64();
    }
  }

  if (PERFETTO_UNLIKELY(!timestamp_found)) {
    PERFETTO_ELOG("Timestamp field not found in FtraceEvent");
    trace_storage_->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return;
  }

  int64_t timestamp = static_cast<int64_t>(raw_timestamp);
  latest_timestamp_ = std::max(timestamp, latest_timestamp_);

  // We don't need to parse this packet, just push it to be sorted with
  // the timestamp.
  if ((false)) // DNS
    trace_sorter_->PushFtraceEvent(cpu, timestamp, std::move(event));
}

}  // namespace trace_processor
}  // namespace perfetto
