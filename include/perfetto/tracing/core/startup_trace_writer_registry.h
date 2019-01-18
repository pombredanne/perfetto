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

#ifndef INCLUDE_PERFETTO_TRACING_CORE_STARTUP_TRACE_WRITER_REGISTRY_H_
#define INCLUDE_PERFETTO_TRACING_CORE_STARTUP_TRACE_WRITER_REGISTRY_H_

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include "perfetto/base/export.h"
#include "perfetto/base/weak_ptr.h"
#include "perfetto/tracing/core/basic_types.h"

namespace perfetto {

class SharedMemoryArbiterImpl;
class StartupTraceWriter;

namespace base {
class TaskRunner;
}  // namespace base

// Embedders can use this registry to create unbound StartupTraceWriters during
// startup, and later bind them all safely to an arbiter and target buffer.
class PERFETTO_EXPORT StartupTraceWriterRegistry
    : public std::enable_shared_from_this<StartupTraceWriterRegistry> {
 public:
  // Creates a new registry. The registry is refcounted because each writer it
  // creates holds a reference to it. It will be destroyed once the producer
  // releases its reference to the registry (e.g. after binding it to an
  // arbiter) and all its associated writers have been destroyed.
  static std::shared_ptr<StartupTraceWriterRegistry> Create();

  ~StartupTraceWriterRegistry();

  // Returns a new unbound StartupTraceWriter. Should only be called while
  // unbound and only on the writer thread.
  std::unique_ptr<StartupTraceWriter> CreateUnboundTraceWriter();

  // Return an unbound StartupTraceWriter back to the registry before it could
  // be bound. The registry will keep this writer alive until the registry is
  // bound to an arbiter (or destroyed itself). This way, its buffered data is
  // retained. Should only be called while unbound. All packets written to the
  // passed writer should have been completed and it should no longer be used to
  // write data after calling this method.
  void ReturnUnboundTraceWriter(std::unique_ptr<StartupTraceWriter>);

  // Binds all StartupTraceWriters created by this registry to the given arbiter
  // and target buffer. Should only be called once. See
  // SharedMemoryArbiter::BindStartupTraceWriterRegistry() for details.
  //
  // Note that the writers may not be bound synchronously if they are
  // concurrently being written to. The registry will retry on the passed
  // TaskRunner until all writers were bound successfully.
  //
  // Should only be called on the task sequence of the passed TaskRunner.
  void BindToArbiter(base::WeakPtr<SharedMemoryArbiterImpl> arbiter,
                     BufferID target_buffer,
                     base::TaskRunner*);

 private:
  friend class StartupTraceWriter;
  friend class StartupTraceWriterTest;

  StartupTraceWriterRegistry();

  // Called by StartupTraceWriter.
  void OnStartupTraceWriterDestroyed(StartupTraceWriter*);

  // Try to bind the remaining unbound writers and post a continuation to
  // |task_runner_| if any writers could not be bound.
  void TryBindWriters();

  base::TaskRunner* task_runner_;

  // Can only be accessed on |task_runner_|.
  base::WeakPtr<SharedMemoryArbiterImpl> arbiter_;

  // Begin lock-protected members.
  std::mutex lock_;
  std::set<StartupTraceWriter*> unbound_writers_;
  std::vector<std::unique_ptr<StartupTraceWriter>> unbound_owned_writers_;
  bool is_bound_ = false;
  BufferID target_buffer_ = 0;
  // End lock-protected members.

  // Keep at the end.
  base::WeakPtrFactory<StartupTraceWriterRegistry> weak_ptr_factory_;
};

}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACING_CORE_STARTUP_TRACE_WRITER_REGISTRY_H_
