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
#include "perfetto/base/time.h"
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

namespace {

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

static std::function<void()> WrapPost(base::TaskRunner* runner,
                                      const std::function<void()>& fn) {
  return [runner, fn] { runner->PostTask(fn); };
}

static void BenchmarkProducer(benchmark::State& state) {
  base::TestTaskRunner task_runner;

#if PERFETTO_BUILDFLAG(PERFETTO_START_DAEMONS)
  TaskRunnerThread service_thread("perfetto.svc");
  service_thread.Start(std::unique_ptr<ServiceDelegate>(
      new ServiceDelegate(TEST_PRODUCER_SOCK_NAME, TEST_CONSUMER_SOCK_NAME)));
#endif

  TaskRunnerThread producer_thread("perfetto.prd");
  std::unique_ptr<FakeProducerDelegate> producer_delegate(
      new FakeProducerDelegate(
          TEST_PRODUCER_SOCK_NAME,
          WrapPost(&task_runner,
                   task_runner.CreateCheckpoint("producer.enabled"))));
  FakeProducerDelegate* producer_delegate_cached = producer_delegate.get();
  producer_thread.Start(std::move(producer_delegate));

  // Once conneced, we can retrieve the inner producer.
  FakeProducer* producer = producer_delegate_cached->producer();

  // Setup the TraceConfig for the consumer.
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(512);

  // Create the buffer for ftrace.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("android.perfetto.FakeProducer");
  ds_config->set_target_buffer(0);

  // The parameters for the producer.
  static constexpr uint32_t kRandomSeed = 42;
  size_t message_count = state.range(0);
  size_t message_bytes = state.range(1);
  size_t mb_per_s = state.range(2);

  size_t messages_per_s = mb_per_s * 1024 * 1024 / message_bytes;
  size_t time_for_messages_ms =
      5000 + (messages_per_s == 0 ? 0 : message_count * 1000 / messages_per_s);

  // Setup the test to use a random number generator.
  ds_config->mutable_for_testing()->set_seed(kRandomSeed);
  ds_config->mutable_for_testing()->set_message_count(message_count);
  ds_config->mutable_for_testing()->set_message_size(message_bytes);
  ds_config->mutable_for_testing()->set_max_messages_per_second(messages_per_s);

  bool is_first_packet = true;
  auto on_readback_complete = task_runner.CreateCheckpoint("readback.complete");
  std::minstd_rand0 rnd_engine(kRandomSeed);
  auto on_consumer_data = [&is_first_packet, &on_readback_complete,
                           &rnd_engine](std::vector<TracePacket> packets,
                                        bool has_more) {
    for (auto& packet : packets) {
      ASSERT_TRUE(packet.Decode());
      ASSERT_TRUE(packet->has_for_testing() || packet->has_clock_snapshot() ||
                  packet->has_trace_config());
      if (packet->has_clock_snapshot() || packet->has_trace_config())
        continue;
      ASSERT_EQ(protos::TracePacket::kTrustedUid,
                packet->optional_trusted_uid_case());
      if (is_first_packet) {
        rnd_engine = std::minstd_rand0(packet->for_testing().seq_value());
        is_first_packet = false;
      } else {
        ASSERT_EQ(packet->for_testing().seq_value(), rnd_engine());
      }
    }

    if (!has_more) {
      is_first_packet = true;
      on_readback_complete();
    }
  };

  // Finally, make the consumer connect to the service.
  auto on_connect = task_runner.CreateCheckpoint("consumer.connected");
  FakeConsumer consumer(trace_config, std::move(on_connect),
                        std::move(on_consumer_data), &task_runner);
  consumer.Connect(TEST_CONSUMER_SOCK_NAME);
  task_runner.RunUntilCheckpoint("consumer.connected");

  consumer.EnableTracing();
  task_runner.RunUntilCheckpoint("producer.enabled");

  uint64_t wall_start_ns = base::GetWallTimeNs().count();
  uint64_t service_start_ns = service_thread.GetThreadCPUTimeNs();
  uint64_t producer_start_ns = producer_thread.GetThreadCPUTimeNs();
  uint64_t iterations = 0;
  for (auto _ : state) {
    auto cname = "produced.and.committed." + std::to_string(iterations++);
    producer->ProduceEventBatch(
        [] {}, WrapPost(&task_runner, task_runner.CreateCheckpoint(cname)));
    task_runner.RunUntilCheckpoint(cname, time_for_messages_ms);
  }
  uint64_t service_ns = service_thread.GetThreadCPUTimeNs() - service_start_ns;
  uint64_t producer_ns =
      producer_thread.GetThreadCPUTimeNs() - producer_start_ns;
  uint64_t wall_ns = base::GetWallTimeNs().count() - wall_start_ns;

  state.counters["Ser CPU"] = benchmark::Counter(100.0 * service_ns / wall_ns);
  state.counters["Ser ns/m"] =
      benchmark::Counter(1.0 * service_ns / message_count);
  state.counters["Pro CPU"] = benchmark::Counter(100.0 * producer_ns / wall_ns);

  // Read back the buffer just to check correctness.
  consumer.ReadTraceData();
  task_runner.RunUntilCheckpoint("readback.complete");
  state.SetBytesProcessed(iterations * message_bytes * message_count);

  consumer.Disconnect();
}

static void BenchmarkConsumer(benchmark::State& state) {
  base::TestTaskRunner task_runner;

#if PERFETTO_BUILDFLAG(PERFETTO_START_DAEMONS)
  TaskRunnerThread service_thread("perfetto.svc");
  service_thread.Start(std::unique_ptr<ServiceDelegate>(
      new ServiceDelegate(TEST_PRODUCER_SOCK_NAME, TEST_CONSUMER_SOCK_NAME)));
#endif

  TaskRunnerThread producer_thread("perfetto.prd");
  std::unique_ptr<FakeProducerDelegate> producer_delegate(
      new FakeProducerDelegate(
          TEST_PRODUCER_SOCK_NAME,
          WrapPost(&task_runner,
                   task_runner.CreateCheckpoint("producer.enabled"))));
  FakeProducerDelegate* producer_delegate_cached = producer_delegate.get();
  producer_thread.Start(std::move(producer_delegate));

  // Once conneced, we can retrieve the inner producer.
  FakeProducer* producer = producer_delegate_cached->producer();

  // Setup the TraceConfig for the consumer.
  static constexpr uint32_t kBufferSizeBytes = 1024 * 1024;
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(kBufferSizeBytes / 1024);

  // Create the buffer for ftrace.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("android.perfetto.FakeProducer");
  ds_config->set_target_buffer(0);

  // The parameters for the producer.
  static constexpr uint32_t kRandomSeed = 42;
  size_t message_bytes = state.range(0);
  size_t mb_per_s = state.range(1);

  size_t message_count = kBufferSizeBytes / message_bytes;
  size_t messages_per_s = mb_per_s * 1024 * 1024 / message_bytes;

  // Setup the test to use a random number generator.
  ds_config->mutable_for_testing()->set_seed(kRandomSeed);
  ds_config->mutable_for_testing()->set_message_count(message_count);
  ds_config->mutable_for_testing()->set_message_size(message_bytes);
  ds_config->mutable_for_testing()->set_max_messages_per_second(messages_per_s);

  bool is_first_packet = true;
  std::function<void()> on_readback_complete;
  auto on_consumer_data = [&is_first_packet, &on_readback_complete](
                              std::vector<TracePacket> packets, bool has_more) {
    for (auto& packet : packets) {
      ASSERT_TRUE(packet.Decode());
    }

    if (!has_more) {
      is_first_packet = true;
      on_readback_complete();
    }
  };

  // Finally, make the consumer connect to the service.
  auto on_connect = task_runner.CreateCheckpoint("consumer.connected");
  FakeConsumer consumer(trace_config, std::move(on_connect),
                        std::move(on_consumer_data), &task_runner);
  consumer.Connect(TEST_CONSUMER_SOCK_NAME);
  task_runner.RunUntilCheckpoint("consumer.connected");

  consumer.EnableTracing();
  task_runner.RunUntilCheckpoint("producer.enabled");

  uint64_t wall_start_ns = base::GetWallTimeNs().count();
  uint64_t service_start_ns = service_thread.GetThreadCPUTimeNs();
  uint64_t consumer_start_ns = base::GetThreadCPUTimeNs().count();
  uint64_t iterations = 0;
  for (auto _ : state) {
    // Called every time there is a mini-batch produced.
    auto cname = "produced.and.committed." + std::to_string(iterations++);
    auto on_minibatch = [&state, &consumer, &on_readback_complete, cname] {
      on_readback_complete = [&state] { state.PauseTiming(); };
      consumer.ReadTraceData();
    };
    producer->ProduceEventBatch(
        WrapPost(&task_runner, on_minibatch),
        WrapPost(&task_runner, task_runner.CreateCheckpoint(cname)));
    task_runner.RunUntilCheckpoint(cname);

    auto readback_cname = "readback.complete." + std::to_string(iterations);
    on_readback_complete = task_runner.CreateCheckpoint(readback_cname);
    consumer.ReadTraceData();
    task_runner.RunUntilCheckpoint(readback_cname);
  }
  uint64_t service_ns = service_thread.GetThreadCPUTimeNs() - service_start_ns;
  uint64_t consumer_ns = base::GetThreadCPUTimeNs().count() - consumer_start_ns;
  uint64_t wall_ns = base::GetWallTimeNs().count() - wall_start_ns;

  state.counters["Ser CPU"] = benchmark::Counter(100.0 * service_ns / wall_ns);
  state.counters["Ser ns/m"] =
      benchmark::Counter(1.0 * service_ns / message_count);
  state.counters["Con CPU"] = benchmark::Counter(100.0 * consumer_ns / wall_ns);

  state.SetBytesProcessed(iterations * message_bytes * message_count);

  consumer.Disconnect();
}
}

static void BM_EndToEnd_Producer_Saturate(benchmark::State& state) {
  BenchmarkProducer(state);
}

BENCHMARK(BM_EndToEnd_Producer_Saturate)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime()
    ->RangeMultiplier(2)
    ->Ranges({{16, 1024 * 1024}, {8, 2048}, {0, 0}});

static void BM_EndToEnd_Producer_ConstantRate(benchmark::State& state) {
  BenchmarkProducer(state);
}

BENCHMARK(BM_EndToEnd_Producer_ConstantRate)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime()
    ->RangeMultiplier(2)
    ->Ranges({{128 * 1024, 256 * 1024}, {128, 256}, {8, 128}});

static void BM_EndToEnd_Consumer_Saturate(benchmark::State& state) {
  BenchmarkConsumer(state);
}

BENCHMARK(BM_EndToEnd_Consumer_Saturate)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime()
    ->RangeMultiplier(2)
    ->Ranges({{8, 64 * 1024}, {0, 0}});

static void BM_EndToEnd_Consumer_ConstantRate(benchmark::State& state) {
  BenchmarkConsumer(state);
}

BENCHMARK(BM_EndToEnd_Consumer_ConstantRate)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime()
    ->RangeMultiplier(2)
    ->Ranges({{8, 8 * 1024}, {32, 128}});

}  // namespace perfetto
