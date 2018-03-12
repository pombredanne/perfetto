// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>
#include <random>

#include "benchmark/benchmark.h"
#include "perfetto/trace/trace_packet.pb.h"
#include "perfetto/trace/trace_packet.pbzero.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "src/base/test/test_task_runner.h"
#include "test/fake_consumer.h"
#include "test/task_runner_thread.h"
#include "test/task_runner_thread_delegates.h"

namespace perfetto {

// If we're building on Android and starting the daemons ourselves,
// create the sockets in a world-writable location.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) && \
    PERFETTO_BUILDFLAG(PERFETTO_START_DAEMONS)
#define TEST_PRODUCER_SOCK_NAME "/data/local/tmp/traced_producer"
#define TEST_CONSUMER_SOCK_NAME "/data/local/tmp/traced_consumer"
#else
#define TEST_PRODUCER_SOCK_NAME PERFETTO_PRODUCER_SOCK_NAME
#define TEST_CONSUMER_SOCK_NAME PERFETTO_CONSUMER_SOCK_NAME
#endif

static void BM_EndToEnd(benchmark::State& state) {
  base::TestTaskRunner task_runner;

  // Setup the TraceConfig for the consumer.
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(512);
  trace_config.set_duration_ms(200);

  // Create the buffer for ftrace.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("android.perfetto.FakeProducer");
  ds_config->set_target_buffer(0);

  // The parameters for the producer.
  static constexpr uint32_t kRandomSeed = 42;
  uint32_t message_count = state.range(0);

  // Setup the test to use a random number generator.
  ds_config->mutable_for_testing()->set_seed(kRandomSeed);
  ds_config->mutable_for_testing()->set_message_count(message_count);

#if PERFETTO_BUILDFLAG(PERFETTO_START_DAEMONS)
  TaskRunnerThread service_thread;
  service_thread.Start(std::unique_ptr<ServiceDelegate>(
      new ServiceDelegate(TEST_PRODUCER_SOCK_NAME, TEST_CONSUMER_SOCK_NAME)));
#endif

  std::function<void()> data_produced;
  TaskRunnerThread producer_thread;
  producer_thread.Start(
      std::unique_ptr<FakeProducerDelegate>(new FakeProducerDelegate(
          TEST_PRODUCER_SOCK_NAME, [&task_runner, &data_produced] {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
            task_runner.PostDelayedTask(data_produced, 1000);
#else
            task_runner.PostTask(data_produced);
#endif
          })));

  uint64_t total = 0;
  std::function<void()> finish;
  std::minstd_rand0 random(kRandomSeed);
  auto function = [&total, &finish, &random](std::vector<TracePacket> packets,
                                             bool has_more) {
    for (auto& packet : packets) {
      ASSERT_TRUE(packet.Decode());
      ASSERT_TRUE(packet->has_for_testing());
      ASSERT_EQ(protos::TracePacket::kTrustedUid,
                packet->optional_trusted_uid_case());
      if (total++ == 0) {
        random = std::minstd_rand0(packet->for_testing().seq_value());
      } else {
        ASSERT_EQ(packet->for_testing().seq_value(), random());
      }
    }

    if (!has_more) {
      total = 0;
      finish();
    }
  };

  // Finally, make the consumer connect to the service.
  auto connect = task_runner.CreateCheckpoint("connected");
  FakeConsumer consumer(trace_config, std::move(connect), std::move(function),
                        &task_runner);
  consumer.Connect(TEST_CONSUMER_SOCK_NAME);
  task_runner.RunUntilCheckpoint("connected");

  while (state.KeepRunning()) {
    std::string produced_name =
        "data.produced." + std::to_string(state.iterations());
    std::string finish_name =
        "no.more.packets." + std::to_string(state.iterations());

    data_produced = task_runner.CreateCheckpoint(produced_name);
    finish = task_runner.CreateCheckpoint(finish_name);

    consumer.EnableTracing();
    task_runner.RunUntilCheckpoint(produced_name);
    consumer.ReadTraceData();
    task_runner.RunUntilCheckpoint(finish_name);
    consumer.FreeBuffers();

    state.SetBytesProcessed(int64_t(state.iterations()) *
                            (sizeof(uint32_t) + 1024) * message_count);
  }
}
BENCHMARK(BM_EndToEnd)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime()
    ->RangeMultiplier(2)
    ->Range(16, 32 << 10);
}
