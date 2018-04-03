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

#include "src/tracing/core/service_impl.h"

#include <string.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "perfetto/base/file_utils.h"
#include "perfetto/base/temp_file.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/shared_memory.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "src/base/test/test_task_runner.h"
#include "src/tracing/test/test_shared_memory.h"

#include "perfetto/trace/test_event.pbzero.h"
#include "perfetto/trace/trace.pb.h"
#include "perfetto/trace/trace_packet.pb.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
using ::testing::_;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::Property;

namespace {

class MockProducer : public Producer {
 public:
  ~MockProducer() override {}

  // Producer implementation.
  MOCK_METHOD0(OnConnect, void());
  MOCK_METHOD0(OnDisconnect, void());
  MOCK_METHOD2(CreateDataSourceInstance,
               void(DataSourceInstanceID, const DataSourceConfig&));
  MOCK_METHOD1(TearDownDataSourceInstance, void(DataSourceInstanceID));
  MOCK_METHOD0(OnTracingStart, void());
  MOCK_METHOD0(OnTracingStop, void());
  MOCK_METHOD3(Flush,
               void(FlushRequestID, const DataSourceInstanceID*, size_t));
};

class MockConsumer : public Consumer {
 public:
  ~MockConsumer() override {}

  // Consumer implementation.
  MOCK_METHOD0(OnConnect, void());
  MOCK_METHOD0(OnDisconnect, void());
  MOCK_METHOD0(OnTracingStop, void());
  MOCK_METHOD2(OnTraceData,
               void(std::vector<TracePacket>* /*packets*/, bool /*has_more*/));

  // gtest doesn't support move-only types. This wrapper is here jut to pass
  // a pointer to the vector (rather than the vector itself) to the mock method.
  void OnTraceData(std::vector<TracePacket> packets, bool has_more) override {
    OnTraceData(&packets, has_more);
  }
};

}  // namespace

class ServiceImplTest : public testing::Test {
 public:
  ServiceImplTest() {
    auto shm_factory =
        std::unique_ptr<SharedMemory::Factory>(new TestSharedMemory::Factory());
    svc.reset(static_cast<ServiceImpl*>(
        Service::CreateInstance(std::move(shm_factory), &task_runner)
            .release()));
  }

  void TearDown() override {
    if (mock_consumer_) {
      EXPECT_CALL(*mock_consumer_, OnDisconnect());
      consumer_endpoint_.reset();
      mock_consumer_.reset();
    }

    if (mock_producer_) {
      EXPECT_CALL(*mock_producer_, OnDisconnect());
      producer_endpoint_.reset();
      mock_producer_.reset();
    }
  }

  void CreateAndConnectMockProducer(const std::string& name, uid_t uid = 42) {
    if (producer_endpoint_) {
      EXPECT_CALL(*mock_producer_, OnDisconnect());
      producer_endpoint_.reset();
      mock_producer_.reset();
    }
    mock_producer_.reset(new MockProducer());
    producer_endpoint_ = svc->ConnectProducer(mock_producer_.get(), uid, name);
    std::string checkpoint_name = "on_producer_connect_" + name;
    auto on_connect = task_runner.CreateCheckpoint(checkpoint_name);
    EXPECT_CALL(*mock_producer_, OnConnect()).WillOnce(Invoke(on_connect));
    task_runner.RunUntilCheckpoint(checkpoint_name);
  }

  void CreateAndConnectMockConsumer() {
    mock_consumer_.reset(new MockConsumer());
    consumer_endpoint_ = svc->ConnectConsumer(mock_consumer_.get());
    auto on_connect = task_runner.CreateCheckpoint("on_consumer_connect");
    EXPECT_CALL(*mock_consumer_, OnConnect()).WillOnce(Invoke(on_connect));
    task_runner.RunUntilCheckpoint("on_consumer_connect");
  }

  void RegisterProducerDataSource(const std::string& name) {
    DataSourceDescriptor ds_desc;
    ds_desc.set_name(name);
    producer_endpoint_->RegisterDataSource(ds_desc);
    task_runner.RunUntilIdle();
  }

  void EnableTracingAndWait(
      const TraceConfig& trace_config,
      base::ScopedFile write_into_file = base::ScopedFile()) {
    static int i = 0;
    std::string checkpoint_name = "on_start_ds_" + std::to_string(i++);
    auto on_start_ds = task_runner.CreateCheckpoint(checkpoint_name);
    EXPECT_CALL(*mock_producer_, OnTracingStart());
    EXPECT_CALL(*mock_producer_, CreateDataSourceInstance(_, _))
        .WillOnce(Invoke([on_start_ds, this](DataSourceInstanceID,
                                             const DataSourceConfig& cfg) {
          producer_buf_id_ = static_cast<BufferID>(cfg.target_buffer());
          on_start_ds();
        }));
    consumer_endpoint_->EnableTracing(trace_config, std::move(write_into_file));
    task_runner.RunUntilCheckpoint(checkpoint_name);
    task_runner.RunUntilIdle();  // For EnableTracing() reply to the consumer.
  }

  bool FlushAndWait(int timeout_ms = 10000) {
    static int i = 0;
    std::string checkpoint_name = "on_flush_" + std::to_string(i++);
    auto on_flush = task_runner.CreateCheckpoint(checkpoint_name);
    bool flush_res = false;
    consumer_endpoint_->Flush(timeout_ms, [on_flush, &flush_res](bool success) {
      flush_res = success;
      on_flush();
    });
    task_runner.RunUntilCheckpoint(checkpoint_name);
    return flush_res;
  }

  void BeginWaitTracingDisabled() {
    auto prod_stop = task_runner.CreateCheckpoint("on_producer_stop");
    EXPECT_CALL(*mock_producer_, TearDownDataSourceInstance(_));
    EXPECT_CALL(*mock_producer_, OnTracingStop()).WillOnce(Invoke(prod_stop));
    auto cons_stop = task_runner.CreateCheckpoint("on_consumer_stop");
    EXPECT_CALL(*mock_consumer_, OnTracingStop()).WillOnce(Invoke(cons_stop));
  }

  void EndWaitTracingDisabled() {
    task_runner.RunUntilCheckpoint("on_producer_stop");
    task_runner.RunUntilCheckpoint("on_consumer_stop");
  }

  void WaitTracingDisabled() {
    BeginWaitTracingDisabled();
    EndWaitTracingDisabled();
  }

  void DisableTracingAndWait() {
    BeginWaitTracingDisabled();
    consumer_endpoint_->DisableTracing();
    EndWaitTracingDisabled();
  }

  std::vector<protos::TracePacket> ReadBuffers() {
    std::vector<protos::TracePacket> decoded_packets;
    static int i = 0;
    std::string checkpoint_name = "on_read_buffers_" + std::to_string(i++);
    auto on_read_buffers = task_runner.CreateCheckpoint(checkpoint_name);
    EXPECT_CALL(*mock_consumer_, OnTraceData(_, _))
        .WillRepeatedly(
            Invoke([&decoded_packets, on_read_buffers](
                       std::vector<TracePacket>* packets, bool has_more) {
              for (TracePacket& packet : *packets) {
                decoded_packets.emplace_back();
                protos::TracePacket* decoded_packet = &decoded_packets.back();
                packet.Decode(decoded_packet);
              }
              if (!has_more)
                on_read_buffers();
            }));
    consumer_endpoint_->ReadBuffers();
    task_runner.RunUntilCheckpoint(checkpoint_name);
    return decoded_packets;
  }

  base::TestTaskRunner task_runner;
  std::unique_ptr<ServiceImpl> svc;
  std::unique_ptr<MockProducer> mock_producer_;
  std::unique_ptr<Service::ProducerEndpoint> producer_endpoint_;
  std::unique_ptr<MockConsumer> mock_consumer_;
  std::unique_ptr<Service::ConsumerEndpoint> consumer_endpoint_;
  BufferID producer_buf_id_ = 0;
};

TEST_F(ServiceImplTest, RegisterAndUnregister) {
  MockProducer mock_producer_1;
  MockProducer mock_producer_2;
  std::unique_ptr<Service::ProducerEndpoint> producer_endpoint_1 =
      svc->ConnectProducer(&mock_producer_1, 123u /* uid */, "mock_producer_1");
  std::unique_ptr<Service::ProducerEndpoint> producer_endpoint_2 =
      svc->ConnectProducer(&mock_producer_2, 456u /* uid */, "mock_producer_2");

  ASSERT_TRUE(producer_endpoint_1);
  ASSERT_TRUE(producer_endpoint_2);

  InSequence seq;
  EXPECT_CALL(mock_producer_1, OnConnect());
  EXPECT_CALL(mock_producer_2, OnConnect());
  task_runner.RunUntilIdle();

  ASSERT_EQ(2u, svc->num_producers());
  ASSERT_EQ(producer_endpoint_1.get(), svc->GetProducer(1));
  ASSERT_EQ(producer_endpoint_2.get(), svc->GetProducer(2));
  ASSERT_EQ(123u, svc->GetProducer(1)->uid_);
  ASSERT_EQ(456u, svc->GetProducer(2)->uid_);

  DataSourceDescriptor ds_desc1;
  ds_desc1.set_name("foo");
  producer_endpoint_1->RegisterDataSource(ds_desc1);

  DataSourceDescriptor ds_desc2;
  ds_desc2.set_name("bar");
  producer_endpoint_2->RegisterDataSource(ds_desc2);

  task_runner.RunUntilIdle();

  producer_endpoint_1->UnregisterDataSource("foo");
  producer_endpoint_2->UnregisterDataSource("bar");

  task_runner.RunUntilIdle();

  EXPECT_CALL(mock_producer_1, OnDisconnect());
  producer_endpoint_1.reset();
  task_runner.RunUntilIdle();
  Mock::VerifyAndClearExpectations(&mock_producer_1);

  ASSERT_EQ(1u, svc->num_producers());
  ASSERT_EQ(nullptr, svc->GetProducer(1));

  EXPECT_CALL(mock_producer_2, OnDisconnect());
  producer_endpoint_2.reset();
  task_runner.RunUntilIdle();
  Mock::VerifyAndClearExpectations(&mock_producer_2);

  ASSERT_EQ(0u, svc->num_producers());
}

TEST_F(ServiceImplTest, EnableAndDisableTracing) {
  CreateAndConnectMockProducer("mock_producer");
  CreateAndConnectMockConsumer();
  RegisterProducerDataSource("foo");
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(128);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("foo");
  ds_config->set_target_buffer(0);
  EnableTracingAndWait(trace_config);
  DisableTracingAndWait();
}

TEST_F(ServiceImplTest, LockdownMode) {
  CreateAndConnectMockConsumer();
  CreateAndConnectMockProducer("mock_producer_sameuid", geteuid());
  RegisterProducerDataSource("foo");

  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(128);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("foo");
  ds_config->set_target_buffer(0);
  trace_config.set_lockdown_mode(
      TraceConfig::LockdownModeOperation::LOCKDOWN_SET);
  EnableTracingAndWait(trace_config);

  MockProducer mock_producer_otheruid;
  std::unique_ptr<Service::ProducerEndpoint> producer_endpoint =
      svc->ConnectProducer(&mock_producer_otheruid, geteuid() + 1,
                           "mock_producer_otheruid");
  EXPECT_CALL(mock_producer_otheruid, OnConnect()).Times(0);
  task_runner.RunUntilIdle();
  Mock::VerifyAndClearExpectations(&mock_producer_otheruid);

  DisableTracingAndWait();
  consumer_endpoint_->FreeBuffers();
  task_runner.RunUntilIdle();

  CreateAndConnectMockProducer("mock_producer_sameuid_2", geteuid());
  RegisterProducerDataSource("foo");

  trace_config.set_lockdown_mode(
      TraceConfig::LockdownModeOperation::LOCKDOWN_CLEAR);
  EnableTracingAndWait(trace_config);

  MockProducer mock_producer_otheruid_2;
  std::unique_ptr<Service::ProducerEndpoint> producer_endpoint_2 =
      svc->ConnectProducer(&mock_producer_otheruid_2, geteuid() + 1,
                           "mock_producer_otheruid_2");
  EXPECT_CALL(mock_producer_otheruid_2, OnConnect()).Times(1);
  task_runner.RunUntilIdle();

  EXPECT_CALL(mock_producer_otheruid_2, OnDisconnect());
  producer_endpoint.reset();
  producer_endpoint_2.reset();
}

TEST_F(ServiceImplTest, DisconnectConsumerWhileTracing) {
  CreateAndConnectMockProducer("mock_producer");
  CreateAndConnectMockConsumer();
  RegisterProducerDataSource("foo");
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(128);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("foo");
  ds_config->set_target_buffer(0);
  EnableTracingAndWait(trace_config);

  // Disconnecting the consumer while tracing should trigger data source
  // teardown.
  auto on_cons_disconnect = task_runner.CreateCheckpoint("on_cons_disconnect");
  EXPECT_CALL(*mock_consumer_, OnDisconnect())
      .WillOnce(Invoke(on_cons_disconnect));
  auto on_prod_teardown = task_runner.CreateCheckpoint("on_prod_teardown");
  EXPECT_CALL(*mock_producer_, TearDownDataSourceInstance(_))
      .WillOnce(InvokeWithoutArgs(on_prod_teardown));
  EXPECT_CALL(*mock_producer_, OnTracingStop());
  consumer_endpoint_.reset();
  mock_consumer_.reset();
  task_runner.RunUntilCheckpoint("on_cons_disconnect");
  task_runner.RunUntilCheckpoint("on_prod_teardown");
}

TEST_F(ServiceImplTest, ReconnectProducerWhileTracing) {
  CreateAndConnectMockConsumer();
  CreateAndConnectMockProducer("mock_producer");
  RegisterProducerDataSource("foo");
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(128);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("foo");
  ds_config->set_target_buffer(0);
  EnableTracingAndWait(trace_config);

  // Disconnecting and reconnecting a producer with a matching data source.
  // The Producer should see that data source getting enabled again.
  CreateAndConnectMockProducer("mock_producer_2");
  EXPECT_CALL(*mock_producer_, OnTracingStart());
  EXPECT_CALL(*mock_producer_, CreateDataSourceInstance(_, _));
  RegisterProducerDataSource("foo");
}

TEST_F(ServiceImplTest, ProducerIDWrapping) {
  base::TestTaskRunner task_runner;
  auto shm_factory =
      std::unique_ptr<SharedMemory::Factory>(new TestSharedMemory::Factory());
  std::unique_ptr<ServiceImpl> svc(static_cast<ServiceImpl*>(
      Service::CreateInstance(std::move(shm_factory), &task_runner).release()));

  std::map<ProducerID, std::pair<std::unique_ptr<MockProducer>,
                                 std::unique_ptr<Service::ProducerEndpoint>>>
      producers;

  auto ConnectProducerAndWait = [&task_runner, &svc, &producers]() {
    char checkpoint_name[32];
    static int checkpoint_num = 0;
    sprintf(checkpoint_name, "on_connect_%d", checkpoint_num++);
    auto on_connect = task_runner.CreateCheckpoint(checkpoint_name);
    std::unique_ptr<MockProducer> producer(new MockProducer());
    std::unique_ptr<Service::ProducerEndpoint> producer_endpoint =
        svc->ConnectProducer(producer.get(), 123u /* uid */, "mock_producer");
    EXPECT_CALL(*producer, OnConnect()).WillOnce(Invoke(on_connect));
    task_runner.RunUntilCheckpoint(checkpoint_name);
    EXPECT_EQ(&*producer_endpoint, svc->GetProducer(svc->last_producer_id_));
    const ProducerID pr_id = svc->last_producer_id_;
    producers.emplace(pr_id, std::make_pair(std::move(producer),
                                            std::move(producer_endpoint)));
    return pr_id;
  };

  auto DisconnectProducerAndWait = [&task_runner,
                                    &producers](ProducerID pr_id) {
    char checkpoint_name[32];
    static int checkpoint_num = 0;
    sprintf(checkpoint_name, "on_disconnect_%d", checkpoint_num++);
    auto on_disconnect = task_runner.CreateCheckpoint(checkpoint_name);
    auto it = producers.find(pr_id);
    PERFETTO_CHECK(it != producers.end());
    EXPECT_CALL(*it->second.first, OnDisconnect())
        .WillOnce(Invoke(on_disconnect));
    producers.erase(pr_id);
    task_runner.RunUntilCheckpoint(checkpoint_name);
  };

  // Connect producers 1-4.
  for (ProducerID i = 1; i <= 4; i++)
    ASSERT_EQ(i, ConnectProducerAndWait());

  // Disconnect producers 1,3.
  DisconnectProducerAndWait(1);
  DisconnectProducerAndWait(3);

  svc->last_producer_id_ = kMaxProducerID - 1;
  ASSERT_EQ(kMaxProducerID, ConnectProducerAndWait());
  ASSERT_EQ(1u, ConnectProducerAndWait());
  ASSERT_EQ(3u, ConnectProducerAndWait());
  ASSERT_EQ(5u, ConnectProducerAndWait());
  ASSERT_EQ(6u, ConnectProducerAndWait());

  // Disconnect all producers to mute spurious callbacks.
  DisconnectProducerAndWait(kMaxProducerID);
  for (ProducerID i = 1; i <= 6; i++)
    DisconnectProducerAndWait(i);
}

TEST_F(ServiceImplTest, WriteIntoFileAndStopOnMaxSize) {
  CreateAndConnectMockProducer("mock_producer");
  CreateAndConnectMockConsumer();
  RegisterProducerDataSource("datasource");

  static const char kPayload[] = "1234567890abcdef-";
  static const int kNumPackets = 10;
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(4096);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("datasource");
  ds_config->set_target_buffer(0);
  trace_config.set_write_into_file(true);
  trace_config.set_file_write_period_ms(1);
  const uint64_t kMaxFileSize = 512;
  trace_config.set_max_file_size_bytes(kMaxFileSize);
  base::TempFile tmp_file = base::TempFile::Create();
  EnableTracingAndWait(trace_config, base::ScopedFile(dup(tmp_file.fd())));

  std::unique_ptr<TraceWriter> writer =
      producer_endpoint_->CreateTraceWriter(producer_buf_id_);
  // All these packets should fit within kMaxFileSize.
  for (int i = 0; i < kNumPackets; i++) {
    auto tp = writer->NewTracePacket();
    std::string payload(kPayload);
    payload.append(std::to_string(i));
    tp->set_for_testing()->set_str(payload.c_str(), payload.size());
  }

  // Finally add a packet that overflows kMaxFileSize. This should cause the
  // implicit stop of the trace and should *not* be written in the trace.
  {
    auto tp = writer->NewTracePacket();
    char big_payload[kMaxFileSize] = "BIG!";
    tp->set_for_testing()->set_str(big_payload, sizeof(big_payload));
  }
  writer->Flush();
  writer.reset();

  DisableTracingAndWait();

  // Verify the contents of the file.
  std::string trace_raw;
  ASSERT_TRUE(base::ReadFile(tmp_file.path().c_str(), &trace_raw));
  protos::Trace trace;
  ASSERT_TRUE(trace.ParseFromString(trace_raw));
  ASSERT_GE(trace.packet_size(), kNumPackets);
  int num_testing_packet = 0;
  for (int i = 0; i < trace.packet_size(); i++) {
    const protos::TracePacket& tp = trace.packet(i);
    if (!tp.has_for_testing())
      continue;
    ASSERT_EQ(kPayload + std::to_string(num_testing_packet++),
              tp.for_testing().str());
  }
}

TEST_F(ServiceImplTest, ExplicitFlush) {
  CreateAndConnectMockProducer("mock_producer");
  CreateAndConnectMockConsumer();
  RegisterProducerDataSource("foo");

  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(128);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("foo");
  ds_config->set_target_buffer(0);
  EnableTracingAndWait(trace_config);

  std::unique_ptr<TraceWriter> writer =
      producer_endpoint_->CreateTraceWriter(producer_buf_id_);
  {
    auto tp = writer->NewTracePacket();
    tp->set_for_testing()->set_str("payload");
  }

  EXPECT_CALL(*mock_producer_, Flush(_, _, 1))
      .WillOnce(Invoke([&writer, this](FlushRequestID flush_req_id,
                                       const DataSourceInstanceID*, size_t) {
        writer->Flush();
        producer_endpoint_->NotifyFlushComplete(flush_req_id);
      }));
  ASSERT_TRUE(FlushAndWait());

  DisableTracingAndWait();
  EXPECT_THAT(
      ReadBuffers(),
      Contains(Property(&protos::TracePacket::for_testing,
                        Property(&protos::TestEvent::str, Eq("payload")))));
}

TEST_F(ServiceImplTest, ImplicitFlushOnTimedTraces) {
  CreateAndConnectMockProducer("mock_producer");
  CreateAndConnectMockConsumer();
  RegisterProducerDataSource("foo");

  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(128);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("foo");
  ds_config->set_target_buffer(0);
  trace_config.set_duration_ms(1);
  EnableTracingAndWait(trace_config);

  std::unique_ptr<TraceWriter> writer =
      producer_endpoint_->CreateTraceWriter(producer_buf_id_);
  {
    auto tp = writer->NewTracePacket();
    tp->set_for_testing()->set_str("payload");
  }

  EXPECT_CALL(*mock_producer_, Flush(_, _, 1))
      .WillOnce(Invoke([&writer, this](FlushRequestID flush_req_id,
                                       const DataSourceInstanceID*, size_t) {
        writer->Flush();
        producer_endpoint_->NotifyFlushComplete(flush_req_id);
      }));

  WaitTracingDisabled();

  EXPECT_THAT(
      ReadBuffers(),
      Contains(Property(&protos::TracePacket::for_testing,
                        Property(&protos::TestEvent::str, Eq("payload")))));
}

}  // namespace perfetto
