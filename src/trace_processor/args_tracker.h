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

#ifndef SRC_TRACE_PROCESSOR_ARGS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_ARGS_TRACKER_H_

#include <unordered_map>

#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class ArgsTracker {
 public:
  using Variadic = TraceStorage::Args::Variadic;

  explicit ArgsTracker(TraceProcessorContext*);

  void AddArg(RowId row_id, StringId flat_key, StringId key, Variadic);
  void Flush();

 private:
  struct RowIdArgs {
    RowId row_id = 0;
    std::array<TraceStorage::Args::Arg, 16> args;
    uint8_t size = 0;
  };

  std::vector<RowIdArgs> row_id_args_;
  TraceProcessorContext* context_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_ARGS_TRACKER_H_
