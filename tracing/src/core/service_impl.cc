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
#include "tracing/core/consumer.h"
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

std::unique_ptr<Service::ConsumerEndpoint> ServiceImpl::ConnectConsumer(
    Consumer* consumer) {
  std::unique_ptr<ConsumerEndpointImpl> endpoint(
      new ConsumerEndpointImpl(this, task_runner_, consumer));
  auto it_and_inserted = consumers_.emplace(endpoint.get());
  PERFETTO_DCHECK(it_and_inserted.second);
  task_runner_->PostTask(std::bind(&Consumer::OnConnect, endpoint->consumer()));
  // TODO: notfy observer.
  return std::move(endpoint);
}

void ServiceImpl::DisconnectConsumer(ConsumerEndpointImpl* consumer) {
  PERFETTO_DCHECK(consumers_.count(consumer));
  consumers_.erase(consumer);
  // TODO: notfy observer.
}

ServiceImpl::ProducerEndpointImpl* ServiceImpl::GetProducer(
    ProducerID id) const {
  auto it = producers_.find(id);
  if (it == producers_.end())
    return nullptr;
  return it->second;
}

void ServiceImpl::SetupLogging(const ConsumerEndpoint::LoggingConfig& cfg) {
  // TODO: this really needs to be more graceful or will UAF. Refcount or
  // something similar.
  log_buffers_.clear();
  for (const auto& buffer_cfg : cfg.buffers) {
    log_buffers_.emplace_back(buffer_cfg.size_kb * 1024);
  }

  std::map<RegisterDataSource*, DataSourceConfig*> data_sources_to_enable;
  for (const auto& cfg_data_source : cfg.data_sources) {
    auto range = data_sources_.equal_range(cfg_data_source.name);
    for (auto it = range.first; it != range.second; it++) {
      const RegisterDataSource& data_source = it.second;
      // TODO match against |producer_name_filter|.
      data_sources_to_enable[&data_source] = cfg_data_source.config;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// ServiceImpl::ConsumerEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////

ServiceImpl::ConsumerEndpointImpl::ConsumerEndpointImpl(
    ServiceImpl* service,
    base::TaskRunner* task_runner,
    Consumer* consumer)
    : service_(service), task_runner_(task_runner), consumer_(consumer) {}

ServiceImpl::ConsumerEndpointImpl::~ConsumerEndpointImpl() {
  consumer_->OnDisconnect();
  service_->DisconnectConsumer(this);
}

void ServiceImpl::ConsumerEndpointImpl::SetupLogging(const LoggingConfig& cfg) {
  service_->SetupLogging(cfg);
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
      producer_(producer),
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
  // TODO the code below is temporary for testing only. It just spits out
  // on stdout the content of the shared memory buffer.

  // Scann all pages and see if there are any complete chunks we can read.
  for (size_t page_idx = 0; page_idx < shmem_abi_.num_pages(); page_idx++) {
    if (shmem_abi_.is_page_free(page_idx))
      continue;

    // Read the page layout.
    bool complete = shmem_abi_.is_page_complete(page_idx);
    auto layout = shmem_abi_.page_layout(page_idx);
    size_t num_chunks = SharedMemoryABI::kNumChunksForLayout[layout];
    printf("  Scanning page: %zu, complete: %d. Page layout: %d (%zu chunks)\n",
           page_idx, complete, layout, num_chunks);

    // Iterate over the chunks in the page.
    for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
      auto state = shmem_abi_.GetChunkState(page_idx, chunk_idx);
      auto chunk = shmem_abi_.TryAcquireChunkForReading(page_idx, chunk_idx);
      // |chunk| may not be valid if it was in a bad state.

      auto* hdr = shmem_abi_.GetChunkHeader(page_idx, chunk_idx);
      auto id = hdr->identifier.load(std::memory_order_relaxed);
      auto packets = hdr->packets.load(std::memory_order_relaxed);
      printf(
          "    Chunk: %zu, WriterID: %u, ChunkID: %u, state: %s, #packets: %u, "
          "flags: %x, acquired_for_reading: %d\n",
          chunk_idx, id.writer_id, id.chunk_id,
          SharedMemoryABI::kChunkStateStr[state], packets.count, packets.flags,
          chunk.is_valid());
      if (!chunk.is_valid())
        continue;

      PERFETTO_DCHECK(chunk.is_valid());
      size_t num_packets = chunk.GetPacketCount();
      uintptr_t ptr = reinterpret_cast<uintptr_t>(chunk.payload_begin());

      // Iterate over all packets.
      printf("    Dumping packets in chunk:\n");

      for (size_t pack_idx = 0; pack_idx < num_packets; pack_idx++) {
        SharedMemoryABI::PacketHeaderType pack_size;
        memcpy(&pack_size, reinterpret_cast<void*>(ptr), sizeof(pack_size));
        ptr += sizeof(pack_size);
        TracePacket proto;
        bool parsed = false;
        // TODO stiching, looks at the flags.
        printf("      #%-3zu len:%u ", pack_idx, pack_size);
        if (ptr > chunk.end_addr() - pack_size) {
          printf("out of bounds!\n");
          break;
        }
        parsed = proto.ParseFromArray(reinterpret_cast<void*>(ptr), pack_size);
        ptr += pack_size;
        printf("\"%s\"\n", parsed ? proto.test().c_str() : "[Parser fail]");
      }
      shmem_abi_.ReleaseChunkAsFree(std::move(chunk));
    }
  }
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

////////////////////////////////////////////////////////////////////////////////
// ServiceImpl::LogBuffer implementation
////////////////////////////////////////////////////////////////////////////////

ServiceImpl::LogBuffer::LogBuffer(size_t s) : size(s) {
  data.reset(new char[size]);
}

ServiceImpl::LogBuffer::LogBuffer(LogBuffer&&) noexcept = default;
ServiceImpl::LogBuffer::~LogBuffer() = default;

}  // namespace perfetto
