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

#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/string_view.h"
#include "perfetto/base/utils.h"
#include "perfetto/protozero/proto_decoder.h"
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/sched_tracker.h"
#include "src/trace_processor/trace_processor_context.h"

#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"

namespace perfetto {
namespace trace_processor {

namespace {

struct PrintEvent {
  char phase;
  uint32_t pid;

  // For phase = 'B' and phase = 'C'
  base::StringView name;

  // For phase = 'C' only.
  int64_t value;
};

bool ParsePrintEvent(base::StringView str, PrintEvent* evt_out) {
  // B|1636|pokeUserActivity
  // E|1636
  // C|1636|wq:monitor|0
 
  // THIS char* IS NOT NULL TERMINATED.
  const char* s = str.data();
  size_t len = str.size();
 
  // Check str matches '[BEC]\|[0-9]+'
  if (len < 3 || s[1] != '|')
    return false;
  size_t pid_length;
  for (size_t i=2;; i++) {
    if (i >= len)
      return false;
    if (s[i] == '|' || s[i] == '\n') {
      pid_length = i - 2;
      break;
    }
    if (s[i] < '0' || s[i] > '9')
      return false;
  }

  std::string pid_str(s+2, pid_length);
  evt_out->pid = static_cast<uint32_t>(std::stoi(pid_str.c_str()));

  evt_out->phase = s[0];
  switch (s[0]) {
    case 'B':
      evt_out->name = base::StringView(s+2+pid_length+1, len-2-pid_length-1);
      return true;
    case 'E':
      return true;
    case 'C':
      return true;
    default:
      return false;
  }
}

} // namespace

using protozero::ProtoDecoder;
using protozero::proto_utils::kFieldTypeLengthDelimited;

ProtoTraceParser::ProtoTraceParser(TraceProcessorContext* context)
    : context_(context) {}

ProtoTraceParser::~ProtoTraceParser() = default;

void ProtoTraceParser::ParseTracePacket(TraceBlobView packet) {
  ProtoDecoder decoder(packet.data(), packet.length());

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::TracePacket::kProcessTreeFieldNumber: {
        const size_t fld_off = packet.offset_of(fld.data());
        ParseProcessTree(packet.slice(fld_off, fld.size()));
        break;
      }
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParseProcessTree(TraceBlobView pstree) {
  ProtoDecoder decoder(pstree.data(), pstree.length());

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    const size_t fld_off = pstree.offset_of(fld.data());
    switch (fld.id) {
      case protos::ProcessTree::kProcessesFieldNumber: {
        ParseProcess(pstree.slice(fld_off, fld.size()));
        break;
      }
      case protos::ProcessTree::kThreadsFieldNumber: {
        ParseThread(pstree.slice(fld_off, fld.size()));
        break;
      }
      default:
        break;
    }
  }
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParseThread(TraceBlobView thread) {
  ProtoDecoder decoder(thread.data(), thread.length());
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

void ProtoTraceParser::ParseProcess(TraceBlobView process) {
  ProtoDecoder decoder(process.data(), process.length());

  uint32_t pid = 0;
  base::StringView process_name;

  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::ProcessTree::Process::kPidFieldNumber:
        pid = fld.as_uint32();
        break;
      case protos::ProcessTree::Process::kCmdlineFieldNumber:
        if (process_name.empty())
          process_name = fld.as_string();
        break;
      default:
        break;
    }
  }
  context_->process_tracker->UpdateProcess(pid, process_name);
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParseFtracePacket(uint32_t cpu,
                                         uint64_t timestamp,
                                         TraceBlobView ftrace) {
  ProtoDecoder decoder(ftrace.data(), ftrace.length());
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::FtraceEvent::kSchedSwitchFieldNumber: {
        PERFETTO_DCHECK(timestamp > 0);
        const size_t fld_off = ftrace.offset_of(fld.data());
        ParseSchedSwitch(cpu, timestamp, ftrace.slice(fld_off, fld.size()));
        break;
      }
      case protos::FtraceEvent::kPrintFieldNumber: {
        PERFETTO_DCHECK(timestamp > 0);
        const size_t fld_off = ftrace.offset_of(fld.data());
        ParsePrint(cpu, timestamp, ftrace.slice(fld_off, fld.size()));
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
                                        TraceBlobView sswitch) {
  ProtoDecoder decoder(sswitch.data(), sswitch.length());

  uint32_t prev_pid = 0;
  uint32_t prev_state = 0;
  base::StringView prev_comm;
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
        prev_comm = fld.as_string();
        break;
      case protos::SchedSwitchFtraceEvent::kNextPidFieldNumber:
        next_pid = fld.as_uint32();
        break;
      default:
        break;
    }
  }
  context_->sched_tracker->PushSchedSwitch(cpu, timestamp, prev_pid, prev_state,
                                           prev_comm, next_pid);
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

void ProtoTraceParser::ParsePrint(uint32_t, uint64_t timestamp,
                                        TraceBlobView print) {
  ProtoDecoder decoder(print.data(), print.length());

  base::StringView buf;
  for (auto fld = decoder.ReadField(); fld.id != 0; fld = decoder.ReadField()) {
    switch (fld.id) {
      case protos::PrintFtraceEvent::kBufFieldNumber:
        buf = fld.as_string();
        break;
      default:
        break;
    }
  }

  ParsePrintEvent(buf, &evt);
  ProcessTracker* procs = context_->process_tracker.get();
  TraceStorage* storage = context_->storage.get();
  TraceStorage::NestableSlices* slices = storage->mutable_nestable_slices();

  PrintEvent evt{};
  switch (evt.phase) {
    case 'B':
      PERFETTO_LOG("%c %u %s", evt.phase, evt.pid, evt.name.ToStdString().c_str());
      StringId name_id = storage->InternString(evt.name);
      UniquePid upid = procs->UpdateProcess(pid);
      break;
    case 'E':
      PERFETTO_LOG("%c %u", evt.phase, evt.pid);

      context_->slices->AddSlice(slice.start_ts, slice.end_ts - slice.start_ts, utid,
                       cat_id, name_id, depth, stack_id, parent_stack_id);
      break;
  }

  
  //context_->sched_tracker->PushSchedSwitch(cpu, timestamp, prev_pid, prev_state,
  //                                         prev_comm, next_pid);
  PERFETTO_DCHECK(decoder.IsEndOfBuffer());
}

}  // namespace trace_processor
}  // namespace perfetto
