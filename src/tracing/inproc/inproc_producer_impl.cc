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

#include "src/tracing/inproc/inproc_producer_impl.h"

#include <inttypes.h>
#include <string.h>

#include "perfetto/base/task_runner.h"
#include "perfetto/tracing/core/commit_data_request.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/shared_memory_arbiter.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/tracing_service.h"

namespace perfetto {

// +---------------------------------------------------------------------------+
// | InprocProducerImpl: the ProducerEndpoint interface implementation.        |
// +---------------------------------------------------------------------------+
// This class is exposed to the embedder and designed to be accessed
// consistently on an arbitrary task runner, which might not match the
// TracingService's one.
// The methods below are invoked by the producer code in the embedder and just
// proxy calls into TracingServiceImpl hopping on its TaskRunner.

InprocProducerImpl::InprocProducerImpl(Producer* producer,
                                       base::TaskRunner* producer_task_runner,
                                       base::TaskRunner* service_task_runner)
    : producer_(producer),
      task_runner_(producer_task_runner),
      svc_task_runner_(service_task_runner),
      svc_proxy_(std::move(endpoint)),
      weak_ptr_factory_(this) {
  PERFETTO_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  // TODO guarantee tehis is constructed on task_runner_;
}

InprocProducerImpl::~InprocProducerImpl() {
  // Destroy the ProducerEndpoint proxy object on the service thread.
  std::shared_ptr<InprocProducerThreadProxy> obj(std::move(svc_proxy_));
  svc_task_runner_->PostTask([obj] {});
}

void InprocProducerImpl::InitializeSharedMemory() {
  PERFETTO_CHECK(!shared_memory_arbiter_);

  // SharedMemoryArbiterImpl takes a (non-owning) pointer to the
  // ProducerEndpoint and the task runner where that lives. It guarantees that
  // all calls to ProducerEndpoint (that are: CommitData(),
  // (Un)RegisterTraceWriter()) are posted on that task runner.
  // We have two options here:
  // 1) Pass |this| and our (Producer) task runner, and hop each request onto
  //    the service via InprocProducerThreadProxy. This involves two PostTask()s
  //    per call (one within SharedMemoryArbiterImpl and one within
  //    InprocProducerImpl::CommitData())
  // 2) Directly pass the real ProducerEndpointImpl created by the core
  //    TracingServiceImpl and its task runner. This keeps one PostTask.
  //
  // Here we opt for 2, because we can guarantee that |this| outlives
  // |svc_proxy_| (InprocProducerThreadProxy) and hence |svc_endpoint|.
  TracingService::ProducerEndpoint* svc_endpoint = svc_proxy_->svc();

  // By design shared_memory(), once created, has indefinite lifetime.
  shmem_ = svc_endpoint->shared_memory();
  shmem_page_size_kb_ = svc_endpoint->shared_buffer_page_size_kb();

  size_t shmem_size = shmem_page_size_kb_ * 1024;
  shared_memory_arbiter_ = SharedMemoryArbiter::CreateInstance(
      shmem_, shmem_size, svc_endpoint, svc_task_runner_);
}

void InprocProducerImpl::RegisterDataSource(const DataSourceDescriptor& desc) {
  TracingService::ProducerEndpoint* svc_endpoint = svc_proxy_->svc();
  svc_task_runner_->PostTask(
      [svc_endpoint, desc] { svc_endpoint->RegisterDataSource(desc); });
}

void InprocProducerImpl::UnregisterDataSource(const std::string& name) {
  TracingService::ProducerEndpoint* svc_endpoint = svc_proxy_->svc();
  svc_task_runner_->PostTask(
      [svc_endpoint, name] { svc_endpoint->UnregisterDataSource(name); });
}

void InprocProducerImpl::RegisterTraceWriter(uint32_t writer_id,
                                             uint32_t target_buffer) {
  TracingService::ProducerEndpoint* svc_endpoint = svc_proxy_->svc();
  svc_task_runner_->PostTask([svc_endpoint, writer_id, target_buffer] {
    svc_endpoint->RegisterTraceWriter(writer_id, target_buffer);
  });
}

void InprocProducerImpl::UnregisterTraceWriter(uint32_t writer_id) {
  TracingService::ProducerEndpoint* svc_endpoint = svc_proxy_->svc();
  svc_task_runner_->PostTask([svc_endpoint, writer_id] {
    svc_endpoint->UnregisterTraceWriter(writer_id);
  });
}

void InprocProducerImpl::CommitData(const CommitDataRequest& req,
                                    CommitDataCallback callback) {
  CommitDataCallback wrapped_cb;
  if (task_runner_ == svc_task_runner_) {
    wrapped_cb = std::move(callback);
  } else {
    base::TaskRunner* producer_task_runner = task_runner_;
    wrapped_cb = [callback, producer_task_runner] {
      producer_task_runner->PostTask(callback);
    };
  }

  TracingService::ProducerEndpoint* svc_endpoint = svc_proxy_->svc();
  svc_task_runner_->PostTask([svc_endpoint, req, wrapped_cb] {
    svc_endpoint->CommitData(req, wrapped_cb);
  });
}

void InprocProducerImpl::NotifyDataSourceStopped(DataSourceInstanceID ds_id) {
  TracingService::ProducerEndpoint* svc_endpoint = svc_proxy_->svc();
  svc_task_runner_->PostTask(
      [svc_endpoint, ds_id] { svc_endpoint->NotifyDataSourceStopped(ds_id); });
}

void InprocProducerImpl::NotifyFlushComplete(FlushRequestID flush_id) {
  shared_memory_arbiter_->NotifyFlushComplete(flush_id);
}

std::unique_ptr<TraceWriter> InprocProducerImpl::CreateTraceWriter(
    BufferID target_buffer) {
  return shared_memory_arbiter_->CreateTraceWriter(target_buffer);
}

SharedMemory* InprocProducerImpl::shared_memory() const {
  return shmem_;
}

size_t InprocProducerImpl::shared_buffer_page_size_kb() const {
  return shmem_page_size_kb_;
}

// +---------------------------------------------------------------------------+
// | Producer interface implementation.                                        |
// +---------------------------------------------------------------------------+
// These methods are invoked by the TracingServiceImpl code and just proxy
// calls onto the real Producer* provided by the client, posting tasks on the
// right TaskRunner.

void InprocProducerThreadProxy::OnConnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto weak_producer = weak_inproc_producer_impl_;
  producer_task_runner_->PostTask([weak_producer] {
    if (weak_producer)
      weak_producer->producer()->OnConnect();
  });
}

void InprocProducerThreadProxy::OnDisconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto weak_producer = weak_inproc_producer_impl_;
  producer_task_runner_->PostTask([weak_producer] {
    if (weak_producer)
      weak_producer->producer()->OnDisconnect();
  });
}

void InprocProducerThreadProxy::OnTracingSetup() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  SharedMemory* shmem = svc_->shared_memory();
  size_t shmem_page_size = svc_->shared_buffer_page_size_kb() * 1024;
  TracingService::ProducerEndpoint* svc = svc_.get();
  PERFETTO_CHECK(shmem && shmem->start());

  auto weak_producer = weak_inproc_producer_impl_;
  producer_task_runner_->PostTask([weak_producer] {
    if (!weak_producer)
      return;
    weak_producer->InitializeSharedMemory(shmem, shmem_page_size);
    weak_producer->producer()->OnTracingSetup();
  });
}

void InprocProducerThreadProxy::SetupDataSource(DataSourceInstanceID ds_id,
                                                const DataSourceConfig& cfg) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto weak_producer = weak_inproc_producer_impl_;
  producer_task_runner_->PostTask([weak_producer, ds_id, cfg] {
    if (weak_producer)
      weak_producer->producer()->SetupDataSource(ds_id, cfg);
  });
}

void InprocProducerThreadProxy::StartDataSource(DataSourceInstanceID ds_id,
                                                const DataSourceConfig& cfg) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto weak_producer = weak_inproc_producer_impl_;
  producer_task_runner_->PostTask([weak_producer, ds_id, cfg] {
    if (weak_producer)
      weak_producer->producer()->StartDataSource(ds_id, cfg);
  });
}

void InprocProducerThreadProxy::StopDataSource(DataSourceInstanceID ds_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto weak_producer = weak_inproc_producer_impl_;
  producer_task_runner_->PostTask([weak_producer, ds_id] {
    if (weak_producer)
      weak_producer->producer()->StopDataSource(ds_id);
  });
}

void InprocProducerThreadProxy::Flush(
    FlushRequestID flush_id,
    const DataSourceInstanceID* data_source_ids,
    size_t num_data_sources) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto weak_producer = weak_inproc_producer_impl_;
  std::vector<DataSourceInstanceID> ds_ids(&data_source_ids[0],
                                           &data_source_ids[num_data_sources]);

  producer_task_runner_->PostTask([weak_producer, flush_id, ds_ids] {
    if (weak_producer)
      weak_producer->producer()->Flush(flush_id, &ds_ids[0], ds_ids.size());
  });
}

}  // namespace perfetto
