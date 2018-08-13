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

#include "src/trace_processor/proto_trace_parser.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/sched_tracker.h"
#include "src/trace_processor/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

using protozero::ProtoDecoder;
using protozero::proto_utils::kFieldTypeLengthDelimited;

ProtoTraceParser::ProtoTraceParser(TraceProcessorContext* context)
    : context_(context){};

ProtoTraceParser::~ProtoTraceParser() = default;

void ProtoTraceParser::ParseTracePacket(const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::TracePacket::kProcessTreeFieldNumber: {
        uint64_t offset = static_cast<uint64_t>(fld.data() - view.buffer.get());
        TraceBlobView process_tree_view = {view.buffer, offset, fld.size()};
        ParseProcessTree(process_tree_view);
        break;
      }
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParseProcessTree(const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::ProcessTree::kProcessesFieldNumber: {
        uint64_t offset = static_cast<uint64_t>(fld.data() - view.buffer.get());
        TraceBlobView process_view = {view.buffer, offset, fld.size()};
        ParseProcess(process_view);
        break;
      }
      case protos::ProcessTree::kThreadsFieldNumber: {
        uint64_t offset = static_cast<uint64_t>(fld.data() - view.buffer.get());
        TraceBlobView thread_view = {view.buffer, offset, fld.size()};
        ParseThread(thread_view);
        break;
      }
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParseThread(const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  uint32_t tid = 0;
  uint32_t tgid = 0;
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::ProcessTree::Thread::kTidFieldNumber:
        tid = fld.as_uint32();
        break;
      case protos::ProcessTree::Thread::kTgidFieldNumber:
        tgid = fld.as_uint32();
        break;
      default:
        break;
    }
  }
  context_->process_tracker->UpdateThread(tid, tgid);

  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParseProcess(const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  uint32_t pid = 0;
  const char* process_name = nullptr;
  size_t process_name_len = 0;
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::ProcessTree::Process::kPidFieldNumber:
        pid = fld.as_uint32();
        break;
      case protos::ProcessTree::Process::kCmdlineFieldNumber:
        if (process_name == nullptr) {
          process_name = fld.as_char_ptr();
          process_name_len = fld.size();
        }
        break;
      default:
        break;
    }
  }
  context_->process_tracker->UpdateProcess(pid, process_name, process_name_len);

  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParseFtracePacket(uint32_t cpu,
                                         uint64_t timestamp,
                                         const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::FtraceEvent::kSchedSwitchFieldNumber: {
        PERFETTO_DCHECK(timestamp > 0);
        uint64_t offset = static_cast<uint64_t>(fld.data() - view.buffer.get());
        TraceBlobView sched_view = {view.buffer, offset, fld.size()};
        ParseSchedSwitch(cpu, timestamp, sched_view);
        break;
      }
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParseSchedSwitch(uint32_t cpu,
                                        uint64_t timestamp,
                                        const TraceBlobView& view) {
  ProtoDecoder decoder(view.buffer.get() + view.offset, view.length);

  uint32_t prev_pid = 0;
  uint32_t prev_state = 0;
  const char* prev_comm = nullptr;
  size_t prev_comm_len = 0;
  uint32_t next_pid = 0;
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::SchedSwitchFtraceEvent::kPrevPidFieldNumber:
        prev_pid = fld.as_uint32();
        break;
      case protos::SchedSwitchFtraceEvent::kPrevStateFieldNumber:
        prev_state = fld.as_uint32();
        break;
      case protos::SchedSwitchFtraceEvent::kPrevCommFieldNumber:
        prev_comm = fld.as_char_ptr();
        prev_comm_len = fld.size();
        break;
      case protos::SchedSwitchFtraceEvent::kNextPidFieldNumber:
        next_pid = fld.as_uint32();
        break;
      default:
        break;
    }
  }
  context_->sched_tracker->PushSchedSwitch(cpu, timestamp, prev_pid, prev_state,
                                           prev_comm, prev_comm_len, next_pid);

  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

}  // namespace trace_processor
}  // namespace perfetto
