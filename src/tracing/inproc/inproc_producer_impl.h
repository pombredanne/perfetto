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

#ifndef SRC_TRACING_INPROC_INPROC_PRODUCER_IMPL_H_
#define SRC_TRACING_INPROC_INPROC_PRODUCER_IMPL_H_

#include <memory>
#include <string>

#include "perfetto/base/thread_checker.h"
#include "perfetto/base/weak_ptr.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/tracing_service.h"

namespace perfetto {

class SharedMemoryArbiter;
class InprocProducerThreadProxy;

// TODO schematic block.
// TODO document destruction order (always InprocProducerImpl -> proxy -> endpoint)

// Instances of this class are returned to the embedder code (outside of
// the perfetto codebase).
// Lifetime of this class is owned by the embedder.
// InprocProducerImpl instances live on the Producer thread which might be
// different than the thread where the TracingServiceImpl lives.
class InprocProducerImpl : public TracingService::ProducerEndpoint {
 public:
  InprocProducerImpl(Producer* producer,
                     base::TaskRunner* producer_task_runner,
                     base::TaskRunner* service_task_runner);
  ~InprocProducerImpl() override;

  void set_service_proxy(std::unique_ptr<InprocProducerThreadProxy> svc_proxy) {
    svc_proxy_ = std::move(svc_proxy);
  }

  // Called by InprocProducerThreadProxy::OnTracingSetup().
  void InitializeSharedMemory();

  // TracingService::ProducerEndpoint implementation.
  // These methods are invoked by the actual Producer(s) code by clients of the
  // tracing library, which know nothing about the IPC transport.
  void RegisterDataSource(const DataSourceDescriptor&) override;
  void UnregisterDataSource(const std::string& name) override;
  void RegisterTraceWriter(uint32_t writer_id, uint32_t target_buffer) override;
  void UnregisterTraceWriter(uint32_t writer_id) override;
  void CommitData(const CommitDataRequest&, CommitDataCallback) override;
  void NotifyDataSourceStopped(DataSourceInstanceID) override;

  std::unique_ptr<TraceWriter> CreateTraceWriter(
      BufferID target_buffer) override;
  void NotifyFlushComplete(FlushRequestID) override;
  SharedMemory* shared_memory() const override;
  size_t shared_buffer_page_size_kb() const override;

  Producer* producer() { return producer_; }

 private:
  Producer* const producer_;

  // The task runner where |producer_| lives. Calls to the Producer interface
  // will be dispatched on this task runner.
  base::TaskRunner* const task_runner_;

  // The task runner where the |svc_| lives. Calls to the ProducerEndpoint
  // interface will be dispatched on this task runner.
  base::TaskRunner* const svc_task_runner_;

  // TODO ownership.
  std::unique_ptr<InprocProducerThreadProxy> svc_proxy_;

  // This is only accessed from |task_runner_|.
  std::unique_ptr<SharedMemoryArbiter> shared_memory_arbiter_;

  // Initialized via InitializrSharedMemory() upon
  // InprocProducerThreadProxy::OnTracingSetup().
  SharedMemory* shmem_ = nullptr;
  size_t shmem_page_size_kb_ = 0;

  base::WeakPtrFactory<InprocProducerImpl> weak_ptr_factory_;  // Keep last.
};

// Producer implementation.
// Its methods are invoked by the TracingServiceImpl code and proxy calls
// to Producer* on the its own task runner.
// Lives on the service thread.
class InprocProducerThreadProxy : public Producer {
 public:
  void OnConnect() override;
  void OnDisconnect() override;
  void OnTracingSetup() override;
  void SetupDataSource(DataSourceInstanceID, const DataSourceConfig&) override;
  void StartDataSource(DataSourceInstanceID, const DataSourceConfig&) override;
  void StopDataSource(DataSourceInstanceID) override;
  void Flush(FlushRequestID,
             const DataSourceInstanceID* data_source_ids,
             size_t num_data_sources) override;

  TracingService::ProducerEndpoint* svc() { return svc_.get(); }
  // base::TaskRunner* task_runner() { return task_runner_; }

 private:
  // The task runner where the TracingService, |svc_| and this class live.
  base::TaskRunner* const task_runner_;

  // |inproc_producer_impl_| lives on |producer_task_runner_|.
  // All calls should be proxied on that thread.
  base::TaskRunner* const producer_task_runner_;
  base::WeakPtr<InprocProducerImpl> weak_inproc_producer_impl_;

  // This will hold the core ProducerEndpointImpl created by TracingServiceImpl.
  std::unique_ptr<TracingService::ProducerEndpoint> svc_;

  PERFETTO_THREAD_CHECKER(thread_checker_)
};

}  // namespace perfetto

#endif  // SRC_TRACING_INPROC_INPROC_PRODUCER_IMPL_H_
