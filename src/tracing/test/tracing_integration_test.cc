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

#include "src/tracing/ipc/posix_shared_memory.h"

#include <inttypes.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"
#include "perfetto/tracing/ipc/producer_ipc_client.h"
#include "perfetto/tracing/ipc/service_ipc_host.h"
#include "src/base/test/test_task_runner.h"
#include "src/ipc/test/test_socket.h"

namespace perfetto {
namespace {

using testing::Invoke;
using testing::_;

constexpr char kProducerSockName[] = TEST_SOCK_NAME("tracing_test-producer");
constexpr char kConsumerSockName[] = TEST_SOCK_NAME("tracing_test-consumer");

class TracingIntegrationTest : public ::testing::Test {
 public:
  void SetUp() override {
    DESTROY_TEST_SOCK(kProducerSockName);
    DESTROY_TEST_SOCK(kConsumerSockName);
    task_runner_.reset(new base::TestTaskRunner());
  }

  void TearDown() override {
    task_runner_.reset();
    DESTROY_TEST_SOCK(kProducerSockName);
    DESTROY_TEST_SOCK(kConsumerSockName);
  }

  std::unique_ptr<base::TestTaskRunner> task_runner_;
};

class MockProducer : public Producer {
 public:
  ~MockProducer() override {}

  // Producer implementation.
  MOCK_METHOD0(OnConnect, void());
  MOCK_METHOD0(OnDisconnect, void());
  MOCK_METHOD2(CreateDataSourceInstance,
               void(DataSourceInstanceID, const DataSourceConfig&));
  MOCK_METHOD1(TearDownDataSourceInstance, void(DataSourceInstanceID));
};

class MockConsumer : public Consumer {
 public:
  ~MockConsumer() override {}

  // Producer implementation.
  MOCK_METHOD0(OnConnect, void());
  MOCK_METHOD0(OnDisconnect, void());
  MOCK_METHOD2(OnTraceData,
               void(const std::vector<TracePacket>&, bool /*has_more*/));
};

TEST_F(TracingIntegrationTest, WithIPCTransport) {
  // Create the service host.
  std::unique_ptr<ServiceIPCHost> svc =
      ServiceIPCHost::CreateInstance(task_runner_.get());
  svc->Start(kProducerSockName, kConsumerSockName);

  // Create and connect a Producer.
  MockProducer producer;
  std::unique_ptr<Service::ProducerEndpoint> producer_endpoint =
      ProducerIPCClient::Connect(kProducerSockName, &producer,
                                 task_runner_.get());
  auto on_producer_connect =
      task_runner_->CreateCheckpoint("on_producer_connect");
  EXPECT_CALL(producer, OnConnect()).WillOnce(Invoke(on_producer_connect));
  task_runner_->RunUntilCheckpoint("on_producer_connect");

  // Register a data source.
  DataSourceDescriptor ds_desc;
  ds_desc.set_name("perfetto.test");
  auto on_data_source_registered =
      task_runner_->CreateCheckpoint("on_data_source_registered");
  producer_endpoint->RegisterDataSource(
      ds_desc, [on_data_source_registered](DataSourceID dsid) {
        PERFETTO_DLOG("Registered data source with ID: %" PRIu64, dsid);
        on_data_source_registered();
      });
  task_runner_->RunUntilCheckpoint("on_data_source_registered");

  // Create and connect a Consumer.
  MockConsumer consumer;
  std::unique_ptr<Service::ConsumerEndpoint> consumer_endpoint =
      ConsumerIPCClient::Connect(kConsumerSockName, &consumer,
                                 task_runner_.get());
  auto on_consumer_connect =
      task_runner_->CreateCheckpoint("on_consumer_connect");
  EXPECT_CALL(consumer, OnConnect()).WillOnce(Invoke(on_consumer_connect));
  task_runner_->RunUntilCheckpoint("on_consumer_connect");

  // Start tracing.
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(4096 * 10);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("perfetto.test");
  ds_config->set_target_buffer(0);
  ds_config->set_trace_category_filters("foo,bar");
  consumer_endpoint->EnableTracing(trace_config);

  // At this point, the Producer should be asked to turn its data source on.
  auto on_create_ds_instance =
      task_runner_->CreateCheckpoint("on_create_ds_instance");
  EXPECT_CALL(producer, CreateDataSourceInstance(_, _))
      .WillOnce(Invoke([on_create_ds_instance](DataSourceInstanceID id,
                                               const DataSourceConfig& cfg) {
        ASSERT_NE(0u, id);
        ASSERT_EQ("perfetto.test", cfg.name());
        ASSERT_EQ(0u, cfg.target_buffer());
        ASSERT_EQ("foo,bar", cfg.trace_category_filters());
        on_create_ds_instance();
      }));
  task_runner_->RunUntilCheckpoint("on_create_ds_instance");

  _exit(0);  // TODO removeme before landing.
}

}  // namespace
}  // namespace perfetto
