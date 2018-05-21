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

#include "perfetto/trace_processor/raw_query.h"

#include <algorithm>
#include <numeric>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/trace_processor/blob_reader.h"
#include "perfetto/trace_processor/db.h"
#include "perfetto/trace_processor/raw_query.pb.h"

namespace perfetto {
namespace trace_processor {

namespace {

// constexpr uint32_t kBlockSize = 1024 * 1024;

}  // namespace

RawQuery::RawQuery(base::TaskRunner* task_runner, BlobReader* reader)
    : task_runner_(task_runner) {
  db_.reset(new DB(task_runner));
  db_->LoadTrace(reader);
}

void RawQuery::Execute(const protos::RawQueryArgs& args,
                       ExecuteCallback callback) {
  db_->Query(args.sql_query().c_str());
  protos::RawQueryResult res;
  task_runner_->PostTask(std::bind(callback, res));
}

}  // namespace trace_processor
}  // namespace perfetto
