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

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "include/perfetto/tracing/core/data_source_descriptor.h"
#include "include/perfetto/tracing/core/producer.h"
#include "include/perfetto/tracing/core/trace_writer.h"
#include "include/perfetto/tracing/ipc/producer_ipc_client.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/utils.h"
#include "perfetto/ipc/host.h"
#include "perfetto/trace/trace_packet.pbzero.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/ipc/service_ipc_host.h"
#include "src/base/test/test_task_runner.h"
#include "test/task_runner_thread.h"

#define PRODUCER_SOCKET "/tmp/perfetto-producer"
#define CONSUMER_SOCKET "/tmp/perfetto-consumer"

class FakeProducer : public perfetto::Producer {
 public:
  FakeProducer(std::string name, const uint8_t* data, size_t size)
      : name_(std::move(name)), data_(data), size_(size) {}

  void Connect(const char* socket_name,
               perfetto::base::TaskRunner* task_runner) {
    endpoint_ =
        perfetto::ProducerIPCClient::Connect(socket_name, this, task_runner);
  }

  void OnConnect() override {
    perfetto::DataSourceDescriptor descriptor;
    descriptor.set_name(name_);
    endpoint_->RegisterDataSource(
        descriptor, [this](perfetto::DataSourceID id) { id_ = id; });
  }

  void OnDisconnect() override {}

  void CreateDataSourceInstance(
      perfetto::DataSourceInstanceID,
      const perfetto::DataSourceConfig& source_config) override {
    auto trace_writer = endpoint_->CreateTraceWriter(
        static_cast<perfetto::BufferID>(source_config.target_buffer()));

    auto packet = trace_writer->NewTracePacket();
    packet->stream_writer_->WriteBytes(data_, size_);
  }

  void TearDownDataSourceInstance(perfetto::DataSourceInstanceID) override {}

 private:
  const std::string name_;
  const uint8_t* data_;
  size_t size_;
  perfetto::DataSourceID id_ = 0;
  std::unique_ptr<perfetto::Service::ProducerEndpoint> endpoint_;
};

class ServiceDelegate : public perfetto::ThreadDelegate {
 public:
  ServiceDelegate() = default;
  ~ServiceDelegate() override = default;
  void Initialize(perfetto::base::TaskRunner* task_runner) override {
    svc_ = perfetto::ServiceIPCHost::CreateInstance(task_runner);
    unlink(PRODUCER_SOCKET);
    unlink(CONSUMER_SOCKET);
    svc_->Start(PRODUCER_SOCKET, CONSUMER_SOCKET);
  }

 private:
  std::unique_ptr<perfetto::ServiceIPCHost> svc_;
  perfetto::base::ScopedFile producer_fd_;
  perfetto::base::ScopedFile consumer_fd_;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  perfetto::TaskRunnerThread service_thread;
  service_thread.Start(std::unique_ptr<ServiceDelegate>(new ServiceDelegate()));
  perfetto::base::TestTaskRunner task_runner;
  FakeProducer producer("fuzzing", data, size);
  producer.Connect(PRODUCER_SOCKET, &task_runner);
  task_runner.RunUntilIdle();
  return 0;
}
