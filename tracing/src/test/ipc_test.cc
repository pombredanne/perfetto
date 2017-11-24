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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "base/logging.h"
#include "base/test/test_task_runner.h"
#include "tracing/core/consumer.h"
#include "tracing/core/data_source_config.h"
#include "tracing/core/data_source_descriptor.h"
#include "tracing/core/producer.h"
#include "tracing/core/service.h"
#include "tracing/core/trace_config.h"
#include "tracing/core/trace_packet.h"
#include "tracing/core/trace_writer.h"
#include "tracing/ipc/producer_ipc_client.h"
#include "tracing/ipc/service_ipc_host.h"
#include "tracing/src/core/service_impl.h"
#include "tracing/src/ipc/consumer/consumer_ipc_client_impl.h"
#include "tracing/src/ipc/posix_shared_memory.h"
#include "tracing/src/ipc/producer/producer_ipc_client_impl.h"
#include "tracing/src/ipc/service/service_ipc_host_impl.h"

// TODO split these, now I need both.
#include "protos/trace_packet.pbzero.h"
// #include "protos/trace_packet.pb.h"

namespace perfetto {

namespace {

const char kProducerSocketName[] = "/tmp/perfetto-ipc-test-producer.sock";
const char kConsumerSocketName[] = "/tmp/perfetto-ipc-test-consumer.sock";

class TestProducer : public Producer {
 public:
  void OnConnect() override {
    PERFETTO_DLOG("Connected as Producer");
    if (on_connect)
      on_connect();
  }

  void OnDisconnect() override {
    PERFETTO_DLOG("Disconnected from tracing service");
  }

  void CreateDataSourceInstance(DataSourceInstanceID dsid,
                                const DataSourceConfig& cfg) override {
    PERFETTO_DLOG(
        "The tracing service requested us to start a new data source %" PRIu64
        ", config: %s",
        dsid, cfg.trace_category_filters.c_str());
    if (on_create_ds)
      on_create_ds(cfg);
  }

  void TearDownDataSourceInstance(DataSourceInstanceID instance_id) override {
    PERFETTO_DLOG(
        "The tracing service requested us to shutdown the data source %" PRIu64,
        instance_id);
  }

  std::function<void()> on_connect;
  std::function<void(const DataSourceConfig&)> on_create_ds;
};

class TestConsumer : public Consumer {
 public:
  void OnConnect() override {
    PERFETTO_DLOG("Connected as Consumer");
    if (on_connect)
      on_connect();
  }

  void OnDisconnect() override {
    PERFETTO_DLOG("Disconnected from tracing service");
  }

  void OnTraceData(const std::vector<TracePacket>& trace_packets) override {
    if (on_trace_data)
      on_trace_data(trace_packets);
  }

  std::function<void()> on_connect;
  std::function<void(const std::vector<TracePacket>&)> on_trace_data;
};

void __attribute__((noreturn)) ProducerMain() {
  base::TestTaskRunner task_runner;
  TestProducer producer;
  std::unique_ptr<Service::ProducerEndpoint> endpoint =
      ProducerIPCClient::Connect(kProducerSocketName, &producer, &task_runner);
  producer.on_connect = task_runner.CreateCheckpoint("connect");
  task_runner.RunUntilCheckpoint("connect");

  DataSourceDescriptor descriptor;
  descriptor.name = "perfetto.test.data_source";
  auto reg_checkpoint = task_runner.CreateCheckpoint("register");
  auto on_register = [reg_checkpoint](DataSourceID id) {
    printf("Service acked RegisterDataSource() with ID %" PRIu64 "\n", id);
    reg_checkpoint();
  };
  endpoint->RegisterDataSource(descriptor, on_register);
  task_runner.RunUntilCheckpoint("register");

  producer.on_create_ds = [&endpoint](const DataSourceConfig& cfg) {
    auto trace_writer1 = endpoint->CreateTraceWriter();
    auto trace_writer2 = endpoint->CreateTraceWriter();
    for (int j = 0; j < 240; j++) {
      auto event = trace_writer1->NewTracePacket();
      char content[64];
      sprintf(content, "Stream 1 - %3d .................", j);
      event->set_test(content);
      event = trace_writer2->NewTracePacket();
      sprintf(content, "Stream 2 - %3d ++++++++++++++++++++++++++++++++++++",
              j);
      event->set_test(content);
    }
  };
  task_runner.Run();
}

void __attribute__((noreturn)) ConsumerMain() {
  base::TestTaskRunner task_runner;
  TestConsumer consumer;
  std::unique_ptr<Service::ConsumerEndpoint> endpoint =
      ConsumerIPCClient::Connect(kConsumerSocketName, &consumer, &task_runner);
  consumer.on_connect = task_runner.CreateCheckpoint("connect");
  task_runner.RunUntilCheckpoint("connect");

  TraceConfig trace_config;
  trace_config.buffers.emplace_back();
  trace_config.buffers.back().size_kb = 1024;
  trace_config.data_sources.emplace_back();
  trace_config.data_sources.back().config.name = "perfetto.test.data_source";
  trace_config.data_sources.back().config.target_buffer = 0;
  trace_config.data_sources.back().config.trace_category_filters = "aa,bb";

  endpoint->StartTracing(trace_config);
  task_runner.RunUntilIdle();

  printf("Press a key to stop tracing...\n");
  getchar();

  consumer.on_trace_data = [](const std::vector<TracePacket>& trace_packets) {
    printf("OnTraceData()\n");
    for (const TracePacket& const_packet : trace_packets) {
      TracePacket& packet = const_cast<TracePacket&>(const_packet);
      bool decoded = packet.Decode();
      printf(" %d %s\n", decoded,
             decoded ? packet->test().c_str() : "[Decode fail]");
    }
  };
  endpoint->StopTracing();
  task_runner.Run();
}

void __attribute__((noreturn)) ServiceMain() {
  unlink(kProducerSocketName);
  unlink(kConsumerSocketName);
  base::TestTaskRunner task_runner;
  std::unique_ptr<ServiceIPCHostImpl> host(static_cast<ServiceIPCHostImpl*>(
      ServiceIPCHost::CreateInstance(&task_runner).release()));

  class Observer : public Service::ObserverForTesting {
   public:
    explicit Observer(ServiceImpl* svc) : svc_(svc) {}
    void OnProducerConnected(ProducerID prid) override {
      printf("Producer connected: ID=%" PRIu64 "\n", prid);
    }

    void OnProducerDisconnected(ProducerID prid) override {
      printf("Producer disconnected: ID=%" PRIu64 "\n", prid);
    }

    void OnDataSourceRegistered(ProducerID prid, DataSourceID dsid) override {
      printf("Data source registered, Producer=%" PRIu64 " DataSource=%" PRIu64
             "\n",
             prid, dsid);
      // DataSourceConfig cfg;
      // cfg.trace_category_filters = "foo,bar";
      // svc_->GetProducer(prid)->producer()->CreateDataSourceInstance(42, cfg);
    }

    void OnDataSourceUnregistered(ProducerID prid, DataSourceID dsid) override {
      printf("Data source unregistered, Producer=%" PRIu64
             " DataSource=%" PRIu64 "\n",
             prid, dsid);
    }

    ServiceImpl* svc_;
  };

  host->Start(kProducerSocketName, kConsumerSocketName);
  Observer observer(static_cast<ServiceImpl*>(host->service_for_testing()));
  host->service_for_testing()->set_observer_for_testing(&observer);
  task_runner.Run();
}

}  // namespace.
}  // namespace perfetto

int main(int argc, char** argv) {
  if (argc == 2 && !strcmp(argv[1], "producer"))
    perfetto::ProducerMain();
  if (argc == 2 && !strcmp(argv[1], "consumer"))
    perfetto::ConsumerMain();
  if (argc == 2 && !strcmp(argv[1], "service"))
    perfetto::ServiceMain();

  fprintf(stderr, "Usage: %s producer | service\n", argv[0]);
  return 1;
}
