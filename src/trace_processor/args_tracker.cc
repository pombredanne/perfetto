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

#include "src/trace_processor/args_tracker.h"

namespace perfetto {
namespace trace_processor {

ArgsTracker::ArgsTracker(TraceProcessorContext* context) : context_(context) {}

void ArgsTracker::AddArg(RowId row_id,
                         StringId flat_key,
                         StringId key,
                         Variadic value) {
  auto it = std::find_if(row_id_args_.begin(), row_id_args_.end(),
                         [row_id](const RowIdArgs& row_id_args) {
                           return row_id_args.row_id == row_id;
                         });

  RowIdArgs* row_id_args;
  if (it == row_id_args_.end()) {
    row_id_args_.emplace_back();
    row_id_args = &row_id_args_.back();
    row_id_args->row_id = row_id;
  } else {
    row_id_args = &*it;
  }

  uint8_t idx = row_id_args->size++;
  PERFETTO_CHECK(idx < row_id_args->args.size());
  row_id_args->args[idx].flat_key = flat_key;
  row_id_args->args[idx].key = key;
  row_id_args->args[idx].value = value;
}

void ArgsTracker::Flush() {
  auto* storage = context_->storage.get();
  for (const auto& row_id_to_arg : row_id_args_) {
    auto set_id = storage->mutable_args()->AddArgs(row_id_to_arg.args,
                                                   row_id_to_arg.size);

    auto pair = TraceStorage::ParseRowId(row_id_to_arg.row_id);
    switch (pair.first) {
      case TableId::kRawEvents:
        storage->mutable_raw_events()->set_arg_set_id(pair.second, set_id);
        break;
      case TableId::kCounters:
        storage->mutable_counters()->set_arg_set_id(pair.second, set_id);
        break;
      default:
        PERFETTO_FATAL("Unsupported table to insert args into");
    }
  }
  row_id_args_.clear();
}

}  // namespace trace_processor
}  // namespace perfetto
