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
  PERFETTO_DCHECK(unbound_writers_.empty());
}

std::unique_ptr<StartupTraceWriter>
StartupTraceWriterRegistry::CreateUnboundTraceWriter() {
  std::lock_guard<std::mutex> lock(lock_);
  PERFETTO_DCHECK(!is_bound_);
  std::unique_ptr<StartupTraceWriter> writer(
      new StartupTraceWriter(shared_from_this()));
  unbound_writers_.insert(writer.get());
  return writer;
}

void StartupTraceWriterRegistry::ReturnUnboundTraceWriter(
    std::unique_ptr<StartupTraceWriter> trace_writer) {
  std::lock_guard<std::mutex> lock(lock_);
  PERFETTO_DCHECK(!is_bound_);
  PERFETTO_DCHECK(!trace_writer->write_in_progress_);
  PERFETTO_DCHECK(unbound_writers_.count(trace_writer.get()));
  unbound_writers_.erase(trace_writer.get());
  unbound_owned_writers_.push_back(std::move(trace_writer));
}

void StartupTraceWriterRegistry::BindToArbiter(
    base::WeakPtr<SharedMemoryArbiterImpl> arbiter,
    BufferID target_buffer,
    base::TaskRunner* task_runner) {
  PERFETTO_DCHECK(arbiter);
  std::vector<std::unique_ptr<StartupTraceWriter>> unbound_owned_writers;
  {
    std::lock_guard<std::mutex> lock(lock_);
    PERFETTO_DCHECK(!is_bound_ && !arbiter_);
    is_bound_ = true;
    arbiter_ = std::move(arbiter);
    target_buffer_ = target_buffer;
    unbound_owned_writers.swap(unbound_owned_writers_);
  }

  // Bind and destroy the owned writers.
  for (const auto& writer : unbound_owned_writers) {
    // This should succeed since nobody can write to these writers concurrently.
    bool success = writer->BindToArbiter(arbiter_.get(), target_buffer_);
    PERFETTO_DCHECK(success);
  }
  unbound_owned_writers.clear();

  task_runner_ = task_runner;
  TryBindWriters();
}

void StartupTraceWriterRegistry::TryBindWriters() {
  // Give up if the arbiter went away since our last attempt.
  if (!arbiter_)
    return;

  std::lock_guard<std::mutex> lock(lock_);
  for (auto it = unbound_writers_.begin(); it != unbound_writers_.end();) {
    if ((*it)->BindToArbiter(arbiter_.get(), target_buffer_)) {
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
