/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "tracing/src/core/service_impl.h"

#include <inttypes.h>

#include <algorithm>

#include "base/logging.h"
#include "base/task_runner.h"
#include "tracing/core/data_source_config.h"
#include "tracing/core/producer.h"
#include "tracing/core/shared_memory.h"

#include "protos/trace_packet.pb.h"

namespace perfetto {

// TODO add ThreadChecker everywhere.

namespace {
constexpr size_t kPageSize = 4096;
constexpr size_t kDefaultShmSize = kPageSize * 16;  // 64 KB.
constexpr size_t kMaxShmSize = kPageSize * 1024;    // 4 MB.
}  // namespace

// static
std::unique_ptr<Service> Service::CreateInstance(
    std::unique_ptr<SharedMemory::Factory> shm_factory,
    base::TaskRunner* task_runner) {
  return std::unique_ptr<Service>(
      new ServiceImpl(std::move(shm_factory), task_runner));
}

ServiceImpl::ServiceImpl(std::unique_ptr<SharedMemory::Factory> shm_factory,
                         base::TaskRunner* task_runner)
    : shm_factory_(std::move(shm_factory)), task_runner_(task_runner) {
  PERFETTO_DCHECK(task_runner_);
}

ServiceImpl::~ServiceImpl() {
  // TODO handle teardown of all Producer.
}

std::unique_ptr<Service::ProducerEndpoint> ServiceImpl::ConnectProducer(
    Producer* producer,
    size_t shared_buffer_page_size_bytes,
    size_t shared_buffer_size_hint_bytes) {
  const ProducerID id = ++last_producer_id_;

  size_t shm_size = std::min(shared_buffer_size_hint_bytes, kMaxShmSize);
  if (shm_size % kPageSize || shm_size < kPageSize)
    shm_size = kDefaultShmSize;

  // TODO(primiano): right now Create() will suicide in case of OOM if the mmap
  // fails. We should instead gracefully fail the request and tell the client
  // to go away.
  auto shared_memory = shm_factory_->CreateSharedMemory(shm_size);
  std::unique_ptr<ProducerEndpointImpl> endpoint(new ProducerEndpointImpl(
      id, this, task_runner_, producer, std::move(shared_memory),
      shared_buffer_page_size_bytes));
  auto it_and_inserted = producers_.emplace(id, endpoint.get());
  PERFETTO_DCHECK(it_and_inserted.second);
  task_runner_->PostTask(std::bind(&Producer::OnConnect, endpoint->producer()));
  if (observer_)
    observer_->OnProducerConnected(id);
  return std::move(endpoint);
}

void ServiceImpl::DisconnectProducer(ProducerID id) {
  PERFETTO_DCHECK(producers_.count(id));
  producers_.erase(id);
  if (observer_)
    observer_->OnProducerDisconnected(id);
}

ServiceImpl::ProducerEndpointImpl* ServiceImpl::GetProducer(
    ProducerID id) const {
  auto it = producers_.find(id);
  if (it == producers_.end())
    return nullptr;
  return it->second;
}

////////////////////////////////////////////////////////////////////////////////
// ServiceImpl::ProducerEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////

ServiceImpl::ProducerEndpointImpl::ProducerEndpointImpl(
    ProducerID id,
    ServiceImpl* service,
    base::TaskRunner* task_runner,
    Producer* producer,
    std::unique_ptr<SharedMemory> shared_memory,
    size_t shared_buffer_page_size_bytes)
    : id_(id),
      service_(service),
      task_runner_(task_runner),
      producer_(std::move(producer)),
      shared_memory_(std::move(shared_memory)),
      shmem_abi_(shared_memory_->start(),
                 shared_memory_->size(),
                 shared_buffer_page_size_bytes) {}

ServiceImpl::ProducerEndpointImpl::~ProducerEndpointImpl() {
  producer_->OnDisconnect();
  service_->DisconnectProducer(id_);
}

void ServiceImpl::ProducerEndpointImpl::RegisterDataSource(
    const DataSourceDescriptor&,
    RegisterDataSourceCallback callback) {
  const DataSourceID dsid = ++last_data_source_id_;
  task_runner_->PostTask(std::bind(std::move(callback), dsid));
  // TODO implement the bookkeeping logic.
  if (service_->observer_)
    service_->observer_->OnDataSourceRegistered(id_, dsid);
}

void ServiceImpl::ProducerEndpointImpl::UnregisterDataSource(
    DataSourceID dsid) {
  PERFETTO_CHECK(dsid);
  // TODO implement the bookkeeping logic.
  if (service_->observer_)
    service_->observer_->OnDataSourceUnregistered(id_, dsid);
}

void ServiceImpl::ProducerEndpointImpl::NotifySharedMemoryUpdate(
    const std::vector<uint32_t>& changed_pages) {
  // TODO implement the bookkeeping logic.
  for (size_t page_idx = 0; page_idx < shmem_abi_.num_pages(); page_idx++) {
    if (shmem_abi_.is_page_free(page_idx))
      continue;
    bool complete = shmem_abi_.is_page_complete(page_idx);
    auto layout = shmem_abi_.page_layout(page_idx);
    size_t num_chunks = SharedMemoryABI::kNumChunksForLayout[layout];
    printf(
        "  Scanning page: %-4zu, complete: %d. Page layout: %d (%zu chunks)\n",
        page_idx, complete, layout, num_chunks);
    for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
      SharedMemoryABI::Chunk chunk;
      bool res = shmem_abi_.TryAcquireChunkForRead(page_idx, chunk_idx, &chunk);
      printf("\n    Chunk: %zu, State: %d, locked for read: %d\n", chunk_idx,
             shmem_abi_.GetChunkState(page_idx, chunk_idx), res);
      if (!res)
        continue;

      PERFETTO_DCHECK(chunk.is_valid());
      size_t num_packets = chunk.GetPacketCount();
      printf("    Num packets: %zu\n", num_packets);
      uintptr_t ptr = reinterpret_cast<uintptr_t>(chunk.payload_begin());
      for (size_t pack_idx = 0; pack_idx < num_packets; pack_idx++) {
        SharedMemoryABI::PacketHeaderType pack_size;
        memcpy(&pack_size, reinterpret_cast<void*>(ptr), sizeof(pack_size));
        ptr += sizeof(pack_size);
        TracePacket proto;
        bool parsed = false;
        // TODO stiching, looks at the flags.
        if (ptr > chunk.end_addr() - pack_size) {
          printf("    #%zu, size:%u, out of bounds!\n", pack_idx, pack_size);
          break;
        }
        parsed = proto.ParseFromArray(reinterpret_cast<void*>(ptr), pack_size);
        ptr += pack_size;
        printf("    #%zu size:%u parsed:%d  content:%s\n", pack_idx, pack_size,
               parsed, proto.test().c_str());
      }
      printf("    Releasing Chunk: %zu as free\n", chunk_idx);
      shmem_abi_.ReleaseChunkAsFree(chunk);
    }
  }
  return;
}

std::unique_ptr<TraceWriter>
ServiceImpl::ProducerEndpointImpl::CreateTraceWriter() {
  // Not implemented. This would be only used in the case of using the core
  // tracing library directly in-process with no IPC layer. It is a legit
  // use case, but just not one we intend to support right now.
  PERFETTO_CHECK(false);
}

void ServiceImpl::set_observer_for_testing(ObserverForTesting* observer) {
  observer_ = observer;
}

SharedMemory* ServiceImpl::ProducerEndpointImpl::shared_memory() const {
  return shared_memory_.get();
}

}  // namespace perfetto
