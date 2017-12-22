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

#define LOG_NDEBUG 0
#define LOG_TAG "PerfettoTest"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "perfetto/base/unix_task_runner.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"

#include "protos/test_event.pbzero.h"
#include "protos/trace_packet.pb.h"
#include "protos/trace_packet.pbzero.h"

using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::_;

namespace perfetto {

class MockConsumer : public Consumer {
 public:
  MockConsumer() {}
  ~MockConsumer() override {}

  // Consumer implementation.
  MOCK_METHOD0(OnConnect, void());
  MOCK_METHOD0(OnDisconnect, void());
  MOCK_METHOD2(DoOnTraceData, void(std::vector<TracePacket>*, bool));

  void OnTraceData(std::vector<TracePacket> packets, bool value) override {
    DoOnTraceData(&packets, value);
  }
};

TEST(PerfettoTest, TestMockProducer) {
  MockConsumer mock_consumer;
  base::UnixTaskRunner task_runner;
  auto client = ConsumerIPCClient::Connect(PERFETTO_CONSUMER_SOCK_NAME, &mock_consumer,
                                           &task_runner);

  EXPECT_CALL(mock_consumer, OnConnect())
      .WillOnce(Invoke([&client, &task_runner]() {
        TraceConfig trace_config;
        trace_config.add_buffers()->set_size_kb(4096 * 10);
        trace_config.set_duration_ms(10000);

        auto* ds_config = trace_config.add_data_sources()->mutable_config();
        ds_config->set_name("android.perfetto.cts.Producer");
        ds_config->set_target_buffer(0);
        ds_config->set_trace_category_filters("foo,bar");

        client->EnableTracing(trace_config);

        task_runner.PostDelayedTask(std::bind([&client]() {
                                      client->DisableTracing();
                                      client->ReadBuffers();
                                    }),
                                    trace_config.duration_ms());
      }));

  auto function = [&task_runner](auto* packets, bool has_more) {
      if (has_more) {
        for (auto& packet : *packets) {
          packet.Decode();
          ASSERT_TRUE(packet->has_test());
          ASSERT_EQ(packet->test(), "test");
        }
        ASSERT_FALSE(packets->empty()); 
      } else {
        ASSERT_TRUE(packets->empty());
        task_runner.Quit();
      }
    };
  EXPECT_CALL(mock_consumer, DoOnTraceData(_, _))
    .WillRepeatedly(Invoke(function));

  task_runner.Run();
}

}  // namespace perfetto
