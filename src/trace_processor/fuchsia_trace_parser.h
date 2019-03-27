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

#ifndef SRC_TRACE_PROCESSOR_FUCHSIA_TRACE_PARSER_H_
#define SRC_TRACE_PROCESSOR_FUCHSIA_TRACE_PARSER_H_

#include "src/trace_processor/chunked_trace_reader.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class TraceProcessorContext;

// The Fuchsia trace format is documented at
// https://fuchsia.googlesource.com/zircon/+/master/docs/tracing/trace_format.md
class FuchsiaTraceParser : public ChunkedTraceReader {
  struct ThreadInfo {
    uint64_t pid;
    uint64_t tid;
  };

  struct OpenSlice {
    int64_t start_ns;
    UniqueTid utid;
    StringId cat;
    StringId name;
  };

  struct ProviderInfo {
    std::string name;

    std::unordered_map<uint64_t, std::string> string_table;
    std::unordered_map<uint64_t, ThreadInfo> thread_table;

    // Stack of open slices per thread per provider.
    std::unordered_map<uint64_t, std::vector<OpenSlice>> slice_stacks;

    uint64_t ticks_per_second = 1000000000;
  };

  struct RunningThread {
    ThreadInfo info;
    int64_t start_ts;
  };

  struct ArgValue {
    enum Type {
      kNull,
      kInt32,
      kUInt32,
      kInt64,
      kUInt64,
      kDouble,
      kString,
      kPointer,
      kKernelObject
    };

    static ArgValue Null() {
      ArgValue variadic;
      variadic.type = kNull;
      return variadic;
    }

    static ArgValue Int32(int32_t i32_value) {
      ArgValue variadic;
      variadic.type = Type::kInt32;
      variadic.i32_value = i32_value;
      return variadic;
    }

    static ArgValue UInt32(uint32_t u32_value) {
      ArgValue variadic;
      variadic.type = Type::kUInt32;
      variadic.u32_value = u32_value;
      return variadic;
    }

    static ArgValue Int64(int64_t i64_value) {
      ArgValue variadic;
      variadic.type = Type::kInt64;
      variadic.i64_value = i64_value;
      return variadic;
    }

    static ArgValue UInt64(uint64_t u64_value) {
      ArgValue variadic;
      variadic.type = Type::kUInt64;
      variadic.u64_value = u64_value;
      return variadic;
    }

    static ArgValue Double(double double_value) {
      ArgValue variadic;
      variadic.type = Type::kDouble;
      variadic.double_value = double_value;
      return variadic;
    }

    static ArgValue String(base::StringView string_value) {
      ArgValue variadic;
      variadic.type = Type::kString;
      variadic.string_value = string_value;
      return variadic;
    }

    static ArgValue Pointer(uint64_t pointer_value) {
      ArgValue variadic;
      variadic.type = Type::kPointer;
      variadic.pointer_value = pointer_value;
      return variadic;
    }

    static ArgValue KernelObject(uint64_t kernel_object_value) {
      ArgValue variadic;
      variadic.type = Type::kKernelObject;
      variadic.kernel_object_value = kernel_object_value;
      return variadic;
    }

    Type type = Type::kNull;
    union {
      int32_t i32_value;
      uint32_t u32_value;
      int64_t i64_value;
      uint64_t u64_value;
      base::StringView string_value = base::StringView();
      double double_value;
      uint64_t pointer_value;
      uint64_t kernel_object_value;
    };
  };

  // Note: Because the Arg name and, if applicable, string value are
  // represented as |StringView|s pointing into the chunk currently being
  // parsed, they are only valid while that chunk is still available.
  struct Arg {
    base::StringView name = base::StringView();
    ArgValue value = ArgValue::Null();
  };

 public:
  explicit FuchsiaTraceParser(TraceProcessorContext*);
  ~FuchsiaTraceParser() override;

  // TraceParser implementation
  bool Parse(std::unique_ptr<uint8_t[]>, size_t) override;

 private:
  // Helper functions
  void RegisterProvider(uint32_t, std::string);
  int64_t TicksToNs(uint64_t ticks, uint64_t ticks_per_second);
  int64_t ReadTimestamp();
  base::StringView DecodeStringRef(uint32_t);
  ThreadInfo DecodeThreadRef(uint32_t);
  Arg ReadArgument();

  TraceProcessorContext* const context_;
  std::vector<uint8_t> buffer_;
  uint64_t* record_;
  uint64_t* current_;

  // Map from tid to pid. Used because in some places we do not get pid info.
  std::unordered_map<uint64_t, uint64_t> pid_table_;
  std::unordered_map<uint32_t, std::unique_ptr<ProviderInfo>> providers_;
  ProviderInfo* current_provider_;

  // The last thread running on each cpu.
  std::unordered_map<uint32_t, RunningThread> cpu_threads_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_FUCHSIA_TRACE_PARSER_H_
