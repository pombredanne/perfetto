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

#include "test/fake_producer.h"

#include "perfetto/base/logging.h"
#include "perfetto/trace/test_event.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/trace_writer.h"

namespace perfetto {

FakeProducer::FakeProducer(const std::string& name) : name_(name) {}
FakeProducer::~FakeProducer() = default;

void FakeProducer::Connect(const char* socket_name,
                           base::TaskRunner* task_runner,
                           std::function<void()> data_produced_callback) {
  task_runner_ = task_runner;
  data_produced_callback_ = std::move(data_produced_callback);
  endpoint_ = ProducerIPCClient::Connect(socket_name, this, task_runner);
}

void FakeProducer::OnConnect() {
  DataSourceDescriptor descriptor;
  descriptor.set_name(name_);
  endpoint_->RegisterDataSource(descriptor,
                                [this](DataSourceID id) { id_ = id; });
}

void FakeProducer::OnDisconnect() {}

void FakeProducer::CreateDataSourceInstance(
    DataSourceInstanceID,
    const DataSourceConfig& source_config) {
  auto trace_writer = endpoint_->CreateTraceWriter(
      static_cast<BufferID>(source_config.target_buffer()));

  batching_helper_.reset(new PacketBatchingHelper(
      task_runner_, std::move(trace_writer), source_config.for_testing(),
      data_produced_callback_));
  batching_helper_->SendBatch();
}

void FakeProducer::TearDownDataSourceInstance(DataSourceInstanceID) {}

FakeProducer::PacketBatchingHelper::PacketBatchingHelper(
    base::TaskRunner* task_runner,
    std::unique_ptr<TraceWriter> writer,
    const TestConfig& config,
    std::function<void()> data_produced_callback)
    : task_runner_(task_runner),
      writer_(std::move(writer)),
      random_(std::minstd_rand0(config.seed())),
      batches_remaining_(config.message_count() / 30),
      last_batch_(config.message_count() % 30),
      data_produced_callback_(data_produced_callback) {}

void FakeProducer::PacketBatchingHelper::SendBatch() {
  size_t count = batches_remaining_ == 0 ? last_batch_ : 30;
  for (size_t i = 0; i < count; i++) {
    auto handle = writer_->NewTracePacket();
    handle->set_for_testing()->set_seq_value(random_());
    handle->Finalize();
  }
  if (batches_remaining_-- > 0) {
    task_runner_->PostDelayedTask(
        std::bind(&PacketBatchingHelper::SendBatch, this), 1);
  } else {
    writer_.reset();
    task_runner_->PostDelayedTask(data_produced_callback_, 1000);
  }
}

}  // namespace perfetto
