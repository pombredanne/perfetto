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

#include "src/trace_processor/fuchsia_trace_parser.h"

#include <inttypes.h>
#include <unordered_map>

#include "perfetto/base/logging.h"
#include "perfetto/base/string_view.h"
#include "src/trace_processor/ftrace_utils.h"
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/slice_tracker.h"
#include "src/trace_processor/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

namespace {
// Record types
const uint32_t kMetadata = 0;
const uint32_t kInitialization = 1;
const uint32_t kString = 2;
const uint32_t kThread = 3;
const uint32_t kEvent = 4;
const uint32_t kKernelObject = 7;
const uint32_t kContextSwitch = 8;

// Metadata types
const uint32_t kProviderInfo = 1;
const uint32_t kProviderSection = 2;
const uint32_t kProviderEvent = 3;

// Provider Events
const uint32_t kProviderEventBufferFilled = 0;

// Event types
const uint32_t kDurationBegin = 2;
const uint32_t kDurationEnd = 3;
const uint32_t kDurationComplete = 4;

// Thread states
const uint32_t kThreadNew = 0;
const uint32_t kThreadRunning = 1;
const uint32_t kThreadSuspended = 2;
const uint32_t kThreadBlocked = 3;
const uint32_t kThreadDying = 4;
const uint32_t kThreadDead = 5;

// Zircon object types
const uint32_t kZxObjTypeProcess = 1;
const uint32_t kZxObjTypeThread = 2;

// Argument types
const uint32_t kArgNull = 0;
const uint32_t kArgS32 = 1;
const uint32_t kArgU32 = 2;
const uint32_t kArgS64 = 3;
const uint32_t kArgU64 = 4;
const uint32_t kArgDouble = 5;
const uint32_t kArgString = 6;
const uint32_t kArgPointer = 7;
const uint32_t kArgKernelObject = 8;

template <class T>
T ReadField(uint64_t word, size_t begin, size_t end) {
  return static_cast<T>((word >> begin) &
                        ((uint64_t(1) << (end - begin + 1)) - 1));
}

// Our slices are essentially unordered. This poses a problem when we want to
// compute the depth field, because at the time we see a slice, we may not
// have seen all the slices containing it. In fact, this is the common case,
// because within a provider, complete slices will be ordered by end time, as
// the event gets written when the duration ends (slices that are represented as
// a begin and an end event have events in the natural order). In rarer
// circumstances, we may not have any order relation at all because a thread
// might have events from multiple providers.
//
// SliceTracker assumes that the slices are given to it in order by start
// timestamp. For code simplicity of the initial version of this importer,
// each time we parse a buffer we'll queue up each of the slices we would add,
// sort them by starting timestamp, and then insert them via SliceTracker.
// |QueuedNestableSlice| has the arguments that need to be passed to
// |AddSlice|.
//
// This strategy isn't perfect. If slices with a containment relationship
// end up in different buffers, then they won't account for each other with
// their depths. However, considering the likely buffer sizes and the size of
// each record, this should affect only a small number of slices in a trace.
struct QueuedNestableSlice {
  int64_t start_ns;
  int64_t duration_ns;
  UniqueTid utid;
  StringId cat;
  StringId name;
};
}  // namespace

FuchsiaTraceParser::FuchsiaTraceParser(TraceProcessorContext* context)
    : context_(context) {
  RegisterProvider(0, "");
}

FuchsiaTraceParser::~FuchsiaTraceParser() = default;

bool FuchsiaTraceParser::Parse(std::unique_ptr<uint8_t[]> data, size_t size) {
  ProcessTracker* procs = context_->process_tracker.get();
  TraceStorage* storage = context_->storage.get();
  SliceTracker* slice_tracker = context_->slice_tracker.get();

  buffer_.insert(buffer_.end(), data.get(), data.get() + size);

  uint64_t* word_buffer = reinterpret_cast<uint64_t*>(buffer_.data());
  size_t buffered_words = buffer_.size() / sizeof(uint64_t);
  size_t word_index = 0;

  std::vector<QueuedNestableSlice> queued_slices;

  while (word_index < buffered_words) {
    record_ = &word_buffer[word_index];
    uint64_t header = record_[0];
    current_ = &record_[1];

    uint32_t record_type = ReadField<uint32_t>(header, 0, 3);
    uint32_t record_size = ReadField<uint32_t>(header, 4, 15);
    if (record_size == 0) {
      PERFETTO_ELOG("Unexpected record of size 0");
      // Fatal error
      return false;
    }
    if (word_index + record_size > buffered_words) {
      break;
    }

    switch (record_type) {
      case kMetadata: {
        uint32_t metadata_type = ReadField<uint32_t>(header, 16, 19);
        switch (metadata_type) {
          case kProviderInfo: {
            uint32_t provider_id = ReadField<uint32_t>(header, 20, 51);
            uint32_t name_len = ReadField<uint32_t>(header, 52, 59);
            std::string name(reinterpret_cast<char*>(&record_[1]), name_len);
            RegisterProvider(provider_id, name);
            break;
          }
          case kProviderSection: {
            uint32_t provider_id = ReadField<uint32_t>(header, 20, 51);
            current_provider_ = providers_[provider_id].get();
            break;
          }
          case kProviderEvent: {
            uint32_t provider_id = ReadField<uint32_t>(header, 20, 51);
            uint32_t event_type = ReadField<uint32_t>(header, 52, 55);
            switch (event_type) {
              case kProviderEventBufferFilled: {
                if (providers_.count(provider_id) > 0) {
                  ProviderInfo* provider = providers_[provider_id].get();
                  // In the case where records were dropped, we may have lost
                  // end events for slices that were open, or begin events for
                  // slices that will be closed in future records. If both are
                  // the case, then we would potentially two unrelated events
                  // together into a full slice. Clearing the slice stacks
                  // prevents these false matchings.
                  provider->slice_stacks.clear();
                }
                break;
              }
            }
            break;
          }
        }
        break;
      }
      case kInitialization: {
        current_provider_->ticks_per_second = record_[1];
        break;
      }
      case kString: {
        uint32_t index = ReadField<uint32_t>(header, 16, 30);
        if (index != 0) {
          uint32_t len = ReadField<uint32_t>(header, 32, 46);
          std::string s(reinterpret_cast<char*>(&record_[1]), len);
          current_provider_->string_table[index] = std::move(s);
        }
        break;
      }
      case kThread: {
        uint32_t index = ReadField<uint32_t>(header, 16, 23);
        if (index != 0) {
          ThreadInfo tinfo;
          tinfo.pid = record_[1];
          tinfo.tid = record_[2];

          current_provider_->thread_table[index] = tinfo;
        }
        break;
      }
      case kEvent: {
        uint32_t event_type = ReadField<uint32_t>(header, 16, 19);
        // TODO(bhamrick): Import arguments
        // uint32_t n_args = ReadField<uint32_t>(header, 20, 23);
        uint32_t thread_ref = ReadField<uint32_t>(header, 24, 31);
        uint32_t cat_ref = ReadField<uint32_t>(header, 32, 47);
        uint32_t name_ref = ReadField<uint32_t>(header, 48, 63);

        // Common fields
        int64_t ts = ReadTimestamp();
        ThreadInfo tinfo = DecodeThreadRef(thread_ref);

        base::StringView cat = DecodeStringRef(cat_ref);
        base::StringView name = DecodeStringRef(name_ref);

        switch (event_type) {
          case kDurationBegin: {
            UniqueTid utid =
                procs->UpdateThread(static_cast<uint32_t>(tinfo.tid),
                                    static_cast<uint32_t>(tinfo.pid));

            OpenSlice new_slice;
            new_slice.start_ns = ts;
            new_slice.utid = utid;
            new_slice.cat = storage->InternString(cat);
            new_slice.name = storage->InternString(name);

            current_provider_->slice_stacks[tinfo.tid].push_back(new_slice);
            break;
          }
          case kDurationEnd: {
            std::vector<OpenSlice>& thread_stack =
                current_provider_->slice_stacks[tinfo.tid];
            if (thread_stack.size() > 0) {
              OpenSlice& open_slice = thread_stack.back();

              QueuedNestableSlice full_slice;
              full_slice.start_ns = open_slice.start_ns;
              full_slice.duration_ns = ts - open_slice.start_ns;
              full_slice.utid = open_slice.utid;
              full_slice.cat = open_slice.cat;
              full_slice.name = open_slice.name;

              queued_slices.push_back(full_slice);
              thread_stack.pop_back();
            } else {
              PERFETTO_DLOG(
                  "Skipping duration end record with empty corresponding stack "
                  "for tid %" PRIu64,
                  tinfo.tid);
            }
            break;
          }
          case kDurationComplete: {
            // Since we're currently not handling args, we exploit the fact that
            // the timestamp is guaranteed to be the last word in the record and
            // skip directly over all the arguments.
            int64_t end_ts = TicksToNs(record_[record_size - 1],
                                       current_provider_->ticks_per_second);
            // TODO(bhamrick): Deal with virtual TIDs (which don't fit into
            // uint32_t)
            UniqueTid utid =
                procs->UpdateThread(static_cast<uint32_t>(tinfo.tid),
                                    static_cast<uint32_t>(tinfo.pid));
            StringId cat_id = storage->InternString(cat);
            StringId name_id = storage->InternString(name);

            QueuedNestableSlice slice;
            slice.start_ns = ts;
            slice.duration_ns = end_ts - ts;
            slice.utid = utid;
            slice.cat = cat_id;
            slice.name = name_id;
            queued_slices.push_back(slice);
            break;
          }
        }
        break;
      }
      case kKernelObject: {
        uint32_t obj_type = ReadField<uint32_t>(header, 16, 23);
        uint32_t name_ref = ReadField<uint32_t>(header, 24, 39);

        uint64_t obj_id = *current_;
        current_++;

        base::StringView name = DecodeStringRef(name_ref);

        switch (obj_type) {
          case kZxObjTypeProcess: {
            // Note: Fuchsia pid/tids are 64 bits but Perfetto's tables only
            // support 32 bits. This is usually not an issue except for
            // artificial koids which have the 2^63 bit set. This is used for
            // things such as virtual threads.
            procs->UpdateProcess(static_cast<uint32_t>(obj_id),
                                 base::Optional<uint32_t>(), name);
            break;
          }
          case kZxObjTypeThread: {
            uint32_t n_args = ReadField<uint32_t>(header, 40, 43);
            uint64_t pid = 0;

            for (uint32_t i = 0; i < n_args; i++) {
              Arg arg = ReadArgument();
              if (arg.name == "process") {
                if (arg.value.type == ArgValue::Type::kKernelObject) {
                  pid = arg.value.kernel_object_value;
                }
              }
            }

            pid_table_[obj_id] = pid;

            UniqueTid utid = procs->UpdateThread(static_cast<uint32_t>(obj_id),
                                                 static_cast<uint32_t>(pid));
            StringId name_id = storage->InternString(name);
            storage->GetMutableThread(utid)->name_id = name_id;
            break;
          }
          default: {
            PERFETTO_DLOG("Skipping Kernel Object record with type %d",
                          obj_type);
            break;
          }
        }
        break;
      }
      case kContextSwitch: {
        uint32_t cpu = ReadField<uint32_t>(header, 16, 23);
        uint32_t outgoing_state = ReadField<uint32_t>(header, 24, 27);
        uint32_t outgoing_thread_ref = ReadField<uint32_t>(header, 28, 35);
        uint32_t incoming_thread_ref = ReadField<uint32_t>(header, 36, 43);
        int32_t outgoing_priority = ReadField<int32_t>(header, 44, 51);
        // int32_t incoming_priority = ReadField<int32_t>(header, 52, 59);

        int64_t ts = ReadTimestamp();

        ThreadInfo outgoing_thread = DecodeThreadRef(outgoing_thread_ref);
        ThreadInfo incoming_thread = DecodeThreadRef(incoming_thread_ref);

        // A thread with priority 0 represents an idle CPU
        if (cpu_threads_.count(cpu) != 0 && outgoing_priority != 0) {
          // TODO(bhamrick): Some early events will fail to associate with their
          // pid because the kernel object info event hasn't been processed yet.
          if (pid_table_.count(outgoing_thread.tid) > 0) {
            outgoing_thread.pid = pid_table_[outgoing_thread.tid];
          }

          UniqueTid utid =
              procs->UpdateThread(static_cast<uint32_t>(outgoing_thread.tid),
                                  static_cast<uint32_t>(outgoing_thread.pid));
          RunningThread previous_thread = cpu_threads_[cpu];

          ftrace_utils::TaskState end_state;
          switch (outgoing_state) {
            case kThreadNew:
            case kThreadRunning: {
              end_state =
                  ftrace_utils::TaskState(ftrace_utils::TaskState::kRunnable);
              break;
            }
            case kThreadBlocked: {
              end_state = ftrace_utils::TaskState(
                  ftrace_utils::TaskState::kInterruptibleSleep);
              break;
            }
            case kThreadSuspended: {
              end_state =
                  ftrace_utils::TaskState(ftrace_utils::TaskState::kStopped);
              break;
            }
            case kThreadDying: {
              end_state =
                  ftrace_utils::TaskState(ftrace_utils::TaskState::kExitZombie);
              break;
            }
            case kThreadDead: {
              end_state =
                  ftrace_utils::TaskState(ftrace_utils::TaskState::kExitDead);
              break;
            }
            default: { break; }
          }

          storage->mutable_slices()->AddSlice(
              cpu, previous_thread.start_ts, ts - previous_thread.start_ts,
              utid, end_state, outgoing_priority);
        }

        RunningThread new_running;
        new_running.info = incoming_thread;
        new_running.start_ts = ts;
        cpu_threads_[cpu] = new_running;
        break;
      }
      default: {
        PERFETTO_DLOG("Skipping record of unknown type %d", record_type);
        break;
      }
    }

    word_index += record_size;
  }

  // Sort and push all the slices queued in this buffer.
  std::sort(
      queued_slices.begin(), queued_slices.end(),
      [](const QueuedNestableSlice& slice1, const QueuedNestableSlice& slice2) {
        return slice1.start_ns < slice2.start_ns;
      });
  for (const auto& slice : queued_slices) {
    slice_tracker->Scoped(slice.start_ns, slice.utid, slice.cat, slice.name,
                          slice.duration_ns);
  }

  buffer_.erase(
      buffer_.begin(),
      buffer_.begin() + static_cast<long>(word_index * sizeof(uint64_t)));

  return true;
}

void FuchsiaTraceParser::RegisterProvider(uint32_t provider_id,
                                          std::string name) {
  std::unique_ptr<ProviderInfo> provider(new ProviderInfo());
  provider->name = name;
  current_provider_ = provider.get();
  providers_[provider_id] = std::move(provider);
}

int64_t FuchsiaTraceParser::TicksToNs(uint64_t ticks,
                                      uint64_t ticks_per_second) {
  return static_cast<int64_t>(ticks * uint64_t(1000000000) / ticks_per_second);
}

int64_t FuchsiaTraceParser::ReadTimestamp() {
  uint64_t ticks = *current_;
  current_++;
  return TicksToNs(ticks, current_provider_->ticks_per_second);
}

base::StringView FuchsiaTraceParser::DecodeStringRef(uint32_t string_ref) {
  // Defaults to the empty string.
  base::StringView ret;
  if (string_ref & 0x8000) {
    // Inline string
    size_t len = string_ref & 0x7FFF;
    size_t string_words = (len + 7) / 8;
    ret = base::StringView(reinterpret_cast<const char*>(current_), len);
    current_ += string_words;
  } else if (string_ref != 0) {
    ret = base::StringView(current_provider_->string_table[string_ref]);
  }
  return ret;
}

FuchsiaTraceParser::ThreadInfo FuchsiaTraceParser::DecodeThreadRef(
    uint32_t thread_ref) {
  ThreadInfo ret;
  if (thread_ref != 0) {
    ret = current_provider_->thread_table[thread_ref];
  } else {
    // Inline thread ref
    ret.pid = *current_;
    current_++;
    ret.tid = *current_;
    current_++;
  }
  return ret;
}

FuchsiaTraceParser::Arg FuchsiaTraceParser::ReadArgument() {
  uint64_t* arg_base = current_;
  uint64_t arg_header = *current_;
  current_++;

  uint32_t arg_type = ReadField<uint32_t>(arg_header, 0, 3);
  uint32_t arg_size = ReadField<uint32_t>(arg_header, 4, 15);
  uint32_t name_ref = ReadField<uint32_t>(arg_header, 16, 31);

  Arg ret;
  ret.name = DecodeStringRef(name_ref);
  switch (arg_type) {
    case kArgNull: {
      ret.value = ArgValue::Null();
      break;
    }
    case kArgS32: {
      int32_t s32_val = ReadField<int32_t>(arg_header, 32, 63);
      ret.value = ArgValue::Int32(s32_val);
      break;
    }
    case kArgU32: {
      uint32_t u32_val = ReadField<uint32_t>(arg_header, 32, 63);
      ret.value = ArgValue::UInt32(u32_val);
      break;
    }
    case kArgS64: {
      int64_t s64_val = static_cast<int64_t>(*current_);
      current_++;
      ret.value = ArgValue::Int64(s64_val);
      break;
    }
    case kArgU64: {
      uint64_t u64_val = *current_;
      current_++;
      ret.value = ArgValue::UInt64(u64_val);
      break;
    }
    case kArgDouble: {
      double double_val;
      memcpy(&double_val, reinterpret_cast<const void*>(current_),
             sizeof(double));
      current_++;
      ret.value = ArgValue::Double(double_val);
      break;
    }
    case kArgString: {
      uint32_t value_ref = ReadField<uint32_t>(arg_header, 32, 47);
      ret.value = ArgValue::String(DecodeStringRef(value_ref));
      break;
    }
    case kArgPointer: {
      uint64_t pointer_value = *current_;
      current_++;
      ret.value = ArgValue::Pointer(pointer_value);
      break;
    }
    case kArgKernelObject: {
      uint64_t kernel_object_value = *current_;
      current_++;
      ret.value = ArgValue::KernelObject(kernel_object_value);
      break;
    }
    default: {
      PERFETTO_DLOG("Skipping unknown argument type %d", arg_type);
      break;
    }
  }
  current_ = arg_base + arg_size;
  return ret;
}

}  // namespace trace_processor
}  // namespace perfetto
