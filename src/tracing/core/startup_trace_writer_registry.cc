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

#include "perfetto/tracing/core/startup_trace_writer_registry.h"

#include <functional>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/tracing/core/startup_trace_writer.h"
#include "src/tracing/core/shared_memory_arbiter_impl.h"

using ChunkHeader = perfetto::SharedMemoryABI::ChunkHeader;

namespace perfetto {

StartupTraceWriterRegistry::StartupTraceWriterRegistry()
    : weak_ptr_factory_(this) {}

StartupTraceWriterRegistry::~StartupTraceWriterRegistry() {
  std::lock_guard<std::mutex> lock(lock_);
  for (auto* writer : unbound_writers_)
    writer->set_registry(nullptr);
}

std::unique_ptr<StartupTraceWriter>
StartupTraceWriterRegistry::CreateTraceWriter() {
  std::lock_guard<std::mutex> lock(lock_);
  std::unique_ptr<StartupTraceWriter> writer;
  if (PERFETTO_LIKELY(arbiter_)) {
    writer.reset(
        new StartupTraceWriter(arbiter_->CreateTraceWriter(target_buffer_)));
  } else {
    writer.reset(new StartupTraceWriter());
    unbound_writers_.insert(writer.get());
  }
  return writer;
}

void StartupTraceWriterRegistry::BindToArbiter(SharedMemoryArbiterImpl* arbiter,
                                               BufferID target_buffer,
                                               base::TaskRunner* task_runner) {
  {
    std::lock_guard<std::mutex> lock(lock_);
    PERFETTO_DCHECK(!arbiter_);
    arbiter_ = arbiter;
    target_buffer_ = target_buffer;
  }
  task_runner_ = task_runner;
  TryBindWriters();
}

void StartupTraceWriterRegistry::TryBindWriters() {
  std::lock_guard<std::mutex> lock(lock_);
  for (auto it = unbound_writers_.begin(); it != unbound_writers_.end();) {
    if ((*it)->BindToArbiter(arbiter_, target_buffer_)) {
      (*it)->set_registry(nullptr);
      it = unbound_writers_.erase(it);
    } else {
      it++;
    }
  }
  if (!unbound_writers_.empty()) {
    auto weak_this = weak_ptr_factory_.GetWeakPtr();
    task_runner_->PostTask([weak_this] {
      if (weak_this)
        weak_this->TryBindWriters();
    });
  }
}

void StartupTraceWriterRegistry::OnStartupTraceWriterDestroyed(
    StartupTraceWriter* trace_writer) {
  std::lock_guard<std::mutex> lock(lock_);
  unbound_writers_.erase(trace_writer);
}

}  // namespace perfetto
