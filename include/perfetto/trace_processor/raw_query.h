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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_RAW_QUERY_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_RAW_QUERY_H_

#include <stdint.h>

#include <functional>
#include <memory>

namespace perfetto {

namespace base {
class TaskRunner;
}

namespace protos {
class RawQueryArgs;
class RawQueryResult;
}  // namespace protos

namespace trace_processor {

class BlobReader;
class DB;

// This implements the RPC methods defined in raw_query.proto.
class RawQuery {
 public:
  RawQuery(base::TaskRunner*, BlobReader*);

  using ExecuteCallback = std::function<void(const protos::RawQueryResult&)>;
  void Execute(const protos::RawQueryArgs&, ExecuteCallback);

 private:
  RawQuery(const RawQuery&) = delete;
  RawQuery& operator=(const RawQuery&) = delete;

  base::TaskRunner* const task_runner_;
  std::unique_ptr<DB> db_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_RAW_QUERY_H_
