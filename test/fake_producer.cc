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

  size_t message_count = source_config.for_testing().message_count();
  std::minstd_rand0 rnd_engine(source_config.for_testing().seed());
  for (size_t i = 0; i < message_count; i++) {
    auto handle = trace_writer->NewTracePacket();
    handle->set_for_testing()->set_seq_value(rnd_engine());

    char payload[1024];
    uint64_t string_size = 1024;
    memset(payload, '.', string_size);
    payload[string_size - 1] = 0;
    handle->set_for_testing()->set_str(payload, string_size);

    handle->Finalize();
  }
  data_produced_callback_();
}

void FakeProducer::TearDownDataSourceInstance(DataSourceInstanceID) {}

}  // namespace perfetto
