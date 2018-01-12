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

#include "fake_consumer.h"

namespace perfetto {

class PerfettoCtsTest : public ::testing::Test {
 protected:
  void TestMockProducer(const std::string& producer_name, uint64_t buffer) {
    base::UnixTaskRunner task_runner;

    TraceConfig trace_config;
    trace_config.add_buffers()->set_size_kb(4096 * 10);
    trace_config.set_duration_ms(1000);

    auto* ds_config = trace_config.add_data_sources()->mutable_config();
    ds_config->set_name(producer_name);
    ds_config->set_target_buffer(buffer);
    ds_config->set_trace_category_filters("foo,bar");

    uint64_t total = 0;
    auto function = [&task_runner, &total](auto* packets, bool has_more) {
      if (has_more) {
        for (auto& packet : *packets) {
          packet.Decode();
          ASSERT_TRUE(packet->has_test());
          ASSERT_EQ(packet->test(), "test");
        }
        total += packets->size();

        // TODO(lalitm): renable this when stiching inside the service is
        // present.
        // ASSERT_FALSE(packets->empty());
      } else {
        ASSERT_EQ(total, 10u);
        ASSERT_TRUE(packets->empty());
        task_runner.Quit();
      }
    };
    EXPECT_CALL(mock_consumer, DoOnTraceData(_, _))
        .Times(AtLeast(2))
        .WillRepeatedly(Invoke(function));

    // Timeout if the test fails.
    task_runner.PostDelayedTask([&task_runner]() { task_runner.Quit(); }, 2000);
    task_runner.Run();
  }
};

TEST_F(PerfettoCtsTest, TestProducerActivity) {
  TestMockProducer("android.perfetto.cts.ProducerActivity", 2);
}

TEST_F(PerfettoCtsTest, TestProducerService) {
  TestMockProducer("android.perfetto.cts.ProducerService", 3);
}

TEST_F(PerfettoCtsTest, TestProducerIsolatedService) {
  TestMockProducer("android.perfetto.cts.ProducerIsolatedService", 4);
}

}  // namespace perfetto