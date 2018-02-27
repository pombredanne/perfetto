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
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/producer.h"
#include "perfetto/tracing/core/shared_memory.h"
#include "src/base/test/test_task_runner.h"
#include "src/tracing/test/test_shared_memory.h"

namespace perfetto {
using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;

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
};

}  // namespace

TEST(ServiceImplTest, RegisterAndUnregister) {
  base::TestTaskRunner task_runner;
  auto shm_factory =
      std::unique_ptr<SharedMemory::Factory>(new TestSharedMemory::Factory());
  std::unique_ptr<ServiceImpl> svc(static_cast<ServiceImpl*>(
      Service::CreateInstance(std::move(shm_factory), &task_runner).release()));
  MockProducer mock_producer_1;
  MockProducer mock_producer_2;
  std::unique_ptr<Service::ProducerEndpoint> producer_endpoint_1 =
      svc->ConnectProducer(&mock_producer_1, 123u /* uid */);
  std::unique_ptr<Service::ProducerEndpoint> producer_endpoint_2 =
      svc->ConnectProducer(&mock_producer_2, 456u /* uid */);

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
  producer_endpoint_1->RegisterDataSource(
      ds_desc1, [&task_runner, &producer_endpoint_1](DataSourceID id) {
        EXPECT_EQ(1u, id);
        task_runner.PostTask(
            std::bind(&Service::ProducerEndpoint::UnregisterDataSource,
                      producer_endpoint_1.get(), id));
      });

  DataSourceDescriptor ds_desc2;
  ds_desc2.set_name("bar");
  producer_endpoint_2->RegisterDataSource(
      ds_desc2, [&task_runner, &producer_endpoint_2](DataSourceID id) {
        EXPECT_EQ(1u, id);
        task_runner.PostTask(
            std::bind(&Service::ProducerEndpoint::UnregisterDataSource,
                      producer_endpoint_2.get(), id));
      });

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

TEST(ServiceImplTest, ProducerIDWrapping) {
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
        svc->ConnectProducer(producer.get(), 123u /* uid */);
    EXPECT_CALL(*producer, OnConnect()).WillOnce(Invoke(on_connect));
    task_runner.RunUntilCheckpoint(checkpoint_name);
    EXPECT_EQ(&*producer_endpoint, svc->GetProducer(svc->last_producer_id_));
    const ProducerID pr_id = svc->last_producer_id_;
    producers.emplace(pr_id, std::make_pair(std::move(producer),
                                            std::move(producer_endpoint)));
    return pr_id;
  };

  auto DisconnectProducerAndWait = [&task_runner, &svc,
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

}  // namespace perfetto
