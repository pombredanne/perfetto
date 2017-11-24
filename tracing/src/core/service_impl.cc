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
#include <sys/mman.h>

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
  auto tracing_session_it = tracing_sessions_.find(consumer);
  if (tracing_session_it != tracing_sessions_.end())
    StopTracing(tracing_session_it->first);
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

void ServiceImpl::StartTracing(ConsumerEndpointImpl* initiator,
                               const ConsumerEndpoint::TraceConfig& cfg) {
  auto it_and_inserted = tracing_sessions_.emplace(initiator, TracingSession());
  if (!it_and_inserted.second) {
    PERFETTO_DLOG("The Consumer has already started a tracing session");
    return;
  }
  TracingSession& tracing_session = it_and_inserted.first->second;

  // Initialize the log buffers.
  tracing_session.trace_buffers.reserve(cfg.buffers.size());
  bool did_allocate_all_buffers = false;
  for (const auto& buffer_cfg : cfg.buffers) {
    did_allocate_all_buffers = false;
    // Find a free slot in the |trace_buffers_| table.
    for (size_t i = 0; i < kMaxTraceBuffers; i++) {
      if (trace_buffers_[i])
        continue;
      trace_buffers_[i].Create(buffer_cfg.size_kb * 1024, 4096 /* page size */);
      tracing_session.trace_buffers.emplace_back(i);
      did_allocate_all_buffers = true;
      break;
    }
  }
  // This can happen if all the kMaxTraceBuffers slots are taken (i.e. we are
  // not talking about OOM here, just creating > kMaxTraceBuffers entries). In
  // this case, free all the previously allocated buffers and abort.
  // TODO: add a test to cover this case, this is quite subtle.
  if (!did_allocate_all_buffers) {
    for (size_t buf_index : tracing_session.trace_buffers)
      trace_buffers_[buf_index].Reset();
    tracing_sessions_.erase(initiator);
    PERFETTO_DLOG("Failed to allocate logging buffers");
    return;
  }

  // Enable the data sources on the producers.
  for (const auto& cfg_data_source : cfg.data_sources) {
    // Scan all the registered data sources with a matching name.
    auto range = data_sources_.equal_range(cfg_data_source.config.name);
    for (auto it = range.first; it != range.second; it++) {
      const RegisteredDataSource& reg_data_source = it->second;
      // TODO match against |producer_name_filter|.

      const ProducerID producer_id = reg_data_source.producer_id;
      auto producer_iter = producers_.find(producer_id);
      PERFETTO_CHECK(producer_iter == producers_.end());
      ProducerEndpointImpl* producer = producer_iter->second;
      DataSourceInstanceID inst_id = ++last_data_source_instance_id_;
      tracing_session.data_source_instances.emplace(producer_id, inst_id);
      producer->producer()->CreateDataSourceInstance(inst_id,
                                                     cfg_data_source.config);
    }
  }
}

void ServiceImpl::StopTracing(ConsumerEndpointImpl* initiator) {
  auto it = tracing_sessions_.find(initiator);
  if (it == tracing_sessions_.end()) {
    PERFETTO_DLOG("No active tracing session found for the Consumer");
    return;
  }
  TracingSession& tracing_session = it->second;
  for (const auto& data_source_inst : tracing_session.data_source_instances) {
    auto producer_it = producers_.find(data_source_inst.first);
    if (producer_it == producers_.end())
      continue;  // This could legitimately happen if a Producer disconnects.
    producer_it->second->producer()->TearDownDataSourceInstance(
        data_source_inst.second);
  }
  // TODO this needs to be more graceful. This will destroy the |trace_buffers|,
  // which will cause some UAF. Refcount the log buffers or similar.
  tracing_sessions_.erase(initiator);
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

void ServiceImpl::ConsumerEndpointImpl::StartTracing(const TraceConfig& cfg) {
  service_->StartTracing(this, cfg);
}

void ServiceImpl::ConsumerEndpointImpl::StopTracing() {
  service_->StopTracing(this);
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
  for (uint32_t page_idx : changed_pages) {
    if (page_idx >= shmem_abi_.num_pages())
      continue;  // The Producer is playing dirty.
    PERFETTO_DLOG("NOTIFY %d ", shmem_abi_.is_page_complete(page_idx));

    if (!shmem_abi_.is_page_complete(page_idx))
      continue;
    if (!shmem_abi_.TryAcquireAllChunksForReading(page_idx))
      continue;
    // TODO: we should start collecting individual chunks from non fully
    // complete pages after a while.

    size_t target_buffer = shmem_abi_.GetTargetBuffer(page_idx);
    printf("  Moving page: %u, into buffer: %zu\n", page_idx, target_buffer);

    if (target_buffer >= kMaxTraceBuffers &&
        service_->trace_buffers_[target_buffer]) {
      // TODO: we should have some stronger check to prevent that the Producer
      // passes |target_buffer| which is valid, but that we never asked it to
      // use. Essentially we want to prevent A malicious producer to inject data
      // into a log buffer that has nothing to do with it.
      // TODO right now the page_size in the SMB and the trace_buffers_ can
      // mismatch Remove the ability to decide the page size on the Producer.
      uint8_t* dst = service_->trace_buffers_[target_buffer].get_next_page();
      memcpy(dst, shmem_abi_.page_start(page_idx), shmem_abi_.page_size());
    }

    shmem_abi_.ReleaseAllChunksAsFree(page_idx);
  }

  // // TODO the code below is temporary for testing only. It just spits out
  // // on stdout the content of the shared memory buffer.
  // // Scan all pages and see if there are any complete chunks we can read.
  // for (size_t page_idx = 0; page_idx < shmem_abi_.num_pages(); page_idx++) {
  //   if (shmem_abi_.is_page_free(page_idx))
  //     continue;
  //
  //   // Read the page layout.
  //   bool complete = shmem_abi_.is_page_complete(page_idx);
  //   auto layout = shmem_abi_.page_layout(page_idx);
  //   size_t num_chunks = SharedMemoryABI::kNumChunksForLayout[layout];
  //   printf("  Scanning page: %zu, complete: %d. Page layout: %d (%zu
  //   chunks)\n",
  //          page_idx, complete, layout, num_chunks);
  //
  //   // Iterate over the chunks in the page.
  //   for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
  //     auto state = shmem_abi_.GetChunkState(page_idx, chunk_idx);
  //     auto chunk = shmem_abi_.TryAcquireChunkForReading(page_idx, chunk_idx);
  //     // |chunk| may not be valid if it was in a bad state.
  //
  //     auto* hdr = shmem_abi_.GetChunkHeader(page_idx, chunk_idx);
  //     auto id = hdr->identifier.load(std::memory_order_relaxed);
  //     auto packets = hdr->packets.load(std::memory_order_relaxed);
  //     printf(
  //         "    Chunk: %zu, WriterID: %u, ChunkID: %u, state: %s, #packets:
  //         %u, " "flags: %x, acquired_for_reading: %d\n", chunk_idx,
  //         id.writer_id, id.chunk_id, SharedMemoryABI::kChunkStateStr[state],
  //         packets.count, packets.flags, chunk.is_valid());
  //     if (!chunk.is_valid())
  //       continue;
  //
  //     PERFETTO_DCHECK(chunk.is_valid());
  //     size_t num_packets = chunk.GetPacketCount();
  //     uintptr_t ptr = reinterpret_cast<uintptr_t>(chunk.payload_begin());
  //
  //     // Iterate over all packets.
  //     printf("    Dumping packets in chunk:\n");
  //
  //     for (size_t pack_idx = 0; pack_idx < num_packets; pack_idx++) {
  //       SharedMemoryABI::PacketHeaderType pack_size;
  //       memcpy(&pack_size, reinterpret_cast<void*>(ptr), sizeof(pack_size));
  //       ptr += sizeof(pack_size);
  //       TracePacket proto;
  //       bool parsed = false;
  //       // TODO stiching, looks at the flags.
  //       printf("      #%-3zu len:%u ", pack_idx, pack_size);
  //       if (ptr > chunk.end_addr() - pack_size) {
  //         printf("out of bounds!\n");
  //         break;
  //       }
  //       parsed = proto.ParseFromArray(reinterpret_cast<void*>(ptr),
  //       pack_size); ptr += pack_size; printf("\"%s\"\n", parsed ?
  //       proto.test().c_str() : "[Parser fail]");
  //     }
  //     shmem_abi_.ReleaseChunkAsFree(std::move(chunk));
  //   }
  // }
}

std::unique_ptr<TraceWriter>
ServiceImpl::ProducerEndpointImpl::CreateTraceWriter(size_t) {
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

ServiceImpl::LogBuffer::LogBuffer() = default;
ServiceImpl::LogBuffer::~LogBuffer() {
  Reset();
}

void ServiceImpl::LogBuffer::Reset() {
  if (data_) {
    int res = munmap(data_, size());
    PERFETTO_DCHECK(res == 0);
    data_ = nullptr;
  }
  size_in_4k_multiples_ = 0;
  page_size_in_4k_multiples_ = 0;
}

void ServiceImpl::LogBuffer::Create(size_t size, size_t page_size) {
  // Check that the caller is not destroying an existing buffer.
  PERFETTO_CHECK(!data_);
  PERFETTO_CHECK(size && (page_size % 4096) == 0 && (size % page_size) == 0);
  const size_t kMaxSize = (1ul << (sizeof(size_in_4k_multiples_) * 8)) * 4096;
  PERFETTO_CHECK(size < kMaxSize);
  size_in_4k_multiples_ = static_cast<uint16_t>(size / 4096);
  page_size_in_4k_multiples_ = static_cast<uint16_t>(page_size / 4096);
  cur_page_ = 0;
  PERFETTO_CHECK(this->size() == size);
  void* mm = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  PERFETTO_CHECK(mm != MAP_FAILED);
  data_ = reinterpret_cast<uint8_t*>(mm);
}

}  // namespace perfetto
