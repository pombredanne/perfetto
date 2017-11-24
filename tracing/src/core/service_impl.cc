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
#include <bitset>

#include "base/logging.h"
#include "base/task_runner.h"
#include "tracing/core/consumer.h"
#include "tracing/core/data_source_config.h"
#include "tracing/core/producer.h"
#include "tracing/core/shared_memory.h"
#include "tracing/core/trace_config.h"
#include "tracing/core/trace_packet.h"

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

Service::ProducerEndpoint* ServiceImpl::GetProducer(ProducerID id) const {
  auto it = producers_.find(id);
  if (it == producers_.end())
    return nullptr;
  return it->second;
}

void ServiceImpl::StartTracing(ConsumerEndpointImpl* initiator,
                               const TraceConfig& cfg) {
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
      PERFETTO_CHECK(producer_iter != producers_.end());
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
  auto weak_consumer = initiator->GetWeakPtr();

  for (size_t buf_idx : tracing_session.trace_buffers) {
    PERFETTO_CHECK(buf_idx < kMaxTraceBuffers && trace_buffers_[buf_idx]);
    TraceBuffer& trace_buffer = trace_buffers_[buf_idx];
    SharedMemoryABI abi(trace_buffer.get_page(0), trace_buffer.size(),
                        trace_buffer.page_size());
    for (size_t page_idx =
             (trace_buffer.cur_page() + 1) % trace_buffer.num_pages();
         page_idx != trace_buffer.cur_page();
         page_idx = (page_idx + 1) % trace_buffer.num_pages()) {
      bool is_free = abi.is_page_free(page_idx);
      printf("Dumping page: %zu (Free: %d)\n", page_idx, is_free);
      if (is_free)
        continue;
      uint32_t layout = abi.page_layout(page_idx);
      size_t num_chunks = abi.GetNumChunksForLayout(layout);
      printf("Num chunks: %zu, header: %s\n", num_chunks,
             abi.page_header_dbg(page_idx).c_str());

      for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
        if (abi.GetChunkState(page_idx, chunk_idx) ==
            SharedMemoryABI::kChunkFree) {
          continue;
        }
        printf("  Chunk %zu\n", chunk_idx);
        auto chunk = abi.GetChunkUnchecked(page_idx, layout, chunk_idx);
        uint16_t num_packets;
        uint8_t flags;
        std::tie(num_packets, flags) = chunk.GetPacketCountAndFlags();
        uintptr_t ptr = reinterpret_cast<uintptr_t>(chunk.payload_begin());

        std::shared_ptr<std::vector<TracePacket>> packets(
            new std::vector<TracePacket>());
        packets->reserve(num_packets);
        for (size_t pack_idx = 0; pack_idx < num_packets; pack_idx++) {
          SharedMemoryABI::PacketHeaderType pack_size;
          memcpy(&pack_size, reinterpret_cast<void*>(ptr), sizeof(pack_size));
          ptr += sizeof(pack_size);
          // TODO stiching, looks at the flags.
          printf("      #%-3zu len:%u \n", pack_idx, pack_size);
          if (ptr > chunk.end_addr() - pack_size) {
            printf("out of bounds!\n");
            break;
          }
          packets->emplace_back(reinterpret_cast<void*>(ptr), pack_size);
          ptr += pack_size;
        }  // for(packet)
        task_runner_->PostTask([weak_consumer, packets]() {
          if (weak_consumer)
            weak_consumer->consumer()->OnTraceData(*packets);
        });
      }  // for(chunk)
    }    // for(page)
  }

  tracing_sessions_.erase(it);

  // TODO this needs to be more graceful. This will destroy the |trace_buffers|,
  // which will cause some UAF. Refcount the log buffers or similar.
  // TODO: destroy the buffer in log_buffers_.
}

////////////////////////////////////////////////////////////////////////////////
// ServiceImpl::ConsumerEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////

ServiceImpl::ConsumerEndpointImpl::ConsumerEndpointImpl(ServiceImpl* service,
                                                        base::TaskRunner*,
                                                        Consumer* consumer)
    : service_(service), consumer_(consumer), weak_ptr_factory_(this) {}

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

base::WeakPtr<ServiceImpl::ConsumerEndpointImpl>
ServiceImpl::ConsumerEndpointImpl::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
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
    const DataSourceDescriptor& desc,
    RegisterDataSourceCallback callback) {
  const DataSourceID dsid = ++last_data_source_id_;
  auto it = service_->data_sources_.emplace(desc.name, RegisteredDataSource());
  it->second.descriptor = desc;
  it->second.producer_id = id_;
  data_source_instances_[dsid] = it;

  // TODO: at this point if any tracing session is started we should check
  // whether the data source should part of any of the ongoing sessions.

  task_runner_->PostTask(std::bind(std::move(callback), dsid));

  if (service_->observer_)
    service_->observer_->OnDataSourceRegistered(id_, dsid);
}

void ServiceImpl::ProducerEndpointImpl::UnregisterDataSource(
    DataSourceID dsid) {
  auto it = data_source_instances_.find(dsid);
  if (!dsid || it == data_source_instances_.end()) {
    PERFETTO_DCHECK(false);
    return;
  }
  // TODO: destruction order. What if service_ gets destroyed?
  // TODO: erase them all in case of premature destruction.
  service_->data_sources_.erase(it->second);
  data_source_instances_.erase(dsid);
  if (service_->observer_)
    service_->observer_->OnDataSourceUnregistered(id_, dsid);
}

void ServiceImpl::ProducerEndpointImpl::NotifySharedMemoryUpdate(
    const std::vector<uint32_t>& changed_pages) {
  for (uint32_t page_idx : changed_pages) {
    printf(
        "  Page header: %s\n",
        std::bitset<32>(shmem_abi_.page_layout(page_idx)).to_string().c_str());

    if (page_idx >= shmem_abi_.num_pages())
      continue;  // The Producer is playing dirty.

    if (!shmem_abi_.is_page_complete(page_idx))
      continue;
    if (!shmem_abi_.TryAcquireAllChunksForReading(page_idx))
      continue;
    // TODO: we should start collecting individual chunks from non fully
    // complete pages after a while.

    size_t target_buffer = shmem_abi_.GetTargetBuffer(page_idx);

    printf("NotifySharedMemoryUpdate(). page: %u, buffer: %zu (%s)\n", page_idx,
           target_buffer,
           service_->trace_buffers_[target_buffer] ? "OK" : "N/A");

    if (target_buffer < kMaxTraceBuffers &&
        service_->trace_buffers_[target_buffer]) {
      // TODO: we should have some stronger check to prevent that the Producer
      // passes |target_buffer| which is valid, but that we never asked it to
      // use. Essentially we want to prevent A malicious producer to inject data
      // into a log buffer that has nothing to do with it.
      // TODO right now the page_size in the SMB and the trace_buffers_ can
      // mismatch Remove the ability to decide the page size on the Producer.
      uint8_t* dst = service_->trace_buffers_[target_buffer].get_next_page();
      printf("  Moving page: %u, into buffer: %zu, %zx.\n", page_idx,
             target_buffer,
             dst - service_->trace_buffers_[target_buffer].get_page(0));
      printf("  Page header: %s\n",
             std::bitset<32>(shmem_abi_.page_layout(page_idx))
                 .to_string()
                 .c_str());

      memcpy(dst, shmem_abi_.page_start(page_idx), shmem_abi_.page_size());
    }

    shmem_abi_.ReleaseAllChunksAsFree(page_idx);
  }
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
// ServiceImpl::TraceBuffer implementation
////////////////////////////////////////////////////////////////////////////////

ServiceImpl::TraceBuffer::TraceBuffer() = default;
ServiceImpl::TraceBuffer::~TraceBuffer() {
  Reset();
}

void ServiceImpl::TraceBuffer::Reset() {
  if (data_) {
    int res = munmap(data_, size());
    PERFETTO_DCHECK(res == 0);
    data_ = nullptr;
  }
  size_in_4k_multiples_ = 0;
  page_size_in_4k_multiples_ = 0;
}

void ServiceImpl::TraceBuffer::Create(size_t size, size_t page_size) {
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
