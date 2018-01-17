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

#include <gtest/gtest.h>
#include <unistd.h>
#include <functional>
#include <thread>

#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"
#include "perfetto/tracing/ipc/service_ipc_host.h"

#include "protos/trace_packet.pb.h"
#include "protos/trace_packet.pbzero.h"

#include "src/base/test/test_task_runner.h"
#include "src/traced/probes/ftrace_producer.h"

#include "test/fake_consumer.h"
#include "test/fake_producer.h"

#if defined(PERFETTO_BUILD_WITH_ANDROID)
#include "perfetto/base/android_task_runner.h"
#endif

namespace perfetto {

#if defined(PERFETTO_BUILD_WITH_ANDROID)
using PlatformTaskRunner = base::AndroidTaskRunner;
#else
using PlatformTaskRunner = base::UnixTaskRunner;
#endif

class PerfettoTest : public ::testing::Test {
 public:
  PerfettoTest() {}
  ~PerfettoTest() override = default;

 protected:
  // We need to use templates because we want to create and detroy
  // objects on the task runner thread. Templates give us an easy
  // way to do that.
  template <typename WorkerHandle>
  class TaskRunnerThread {
   public:
    TaskRunnerThread() {}
    ~TaskRunnerThread() {
      std::unique_lock<std::mutex> lock(mutex_);
      if (runner_)
        runner_->Quit();
      lock.unlock();

      if (thread_.joinable())
        thread_.join();
    }

    void Run() {
      // Begin holding the lock for the condition variable.
      std::condition_variable ready;
      std::unique_lock<std::mutex> outer_lock(mutex_);

      // Create and start the background thread.
      thread_ = std::thread([this, &ready]() {
        // Create the task runner and execute the specicalised code.
        base::PlatformTaskRunner task_runner;
        WorkerHandle handle(&task_runner);

        // Notify the main thread that the runner is ready.
        std::unique_lock<std::mutex> lock(mutex_);
        runner_ = &task_runner;
        lock.unlock();
        ready.notify_one();

        // Spin the loop.
        task_runner.Run();
      });

      // Wait for runner to be ready.
      ready.wait(outer_lock, [this]() { return runner_ != nullptr; });
    }

   private:
    std::thread thread_;

    // All variables below this point are protected by |mutex_|.
    std::mutex mutex_;
    base::PlatformTaskRunner* runner_ = nullptr;
  };

  class ServiceHandle {
   public:
    ServiceHandle(base::TaskRunner* task_runner) {
      svc_ = ServiceIPCHost::CreateInstance(task_runner);
      unlink(PERFETTO_PRODUCER_SOCK_NAME);
      unlink(PERFETTO_CONSUMER_SOCK_NAME);
      svc_->Start(PERFETTO_PRODUCER_SOCK_NAME, PERFETTO_CONSUMER_SOCK_NAME);
    }
    ~ServiceHandle() = default;

   private:
    std::unique_ptr<ServiceIPCHost> svc_ = nullptr;
  };

  class FtraceProducerHandle {
   public:
    FtraceProducerHandle(base::TaskRunner* task_runner) {
      producer_.Connect(task_runner);
    }
    ~FtraceProducerHandle() = default;

   private:
    FtraceProducer producer_;
  };

  class FakeProducerHandle {
   public:
    FakeProducerHandle(base::TaskRunner* task_runner)
        : producer_("android.perfetto.FakeProducer") {
      producer_.Connect(task_runner);
    }
    ~FakeProducerHandle() = default;

   private:
    FakeProducer producer_;
  };
};

// TODO(lalitm): reenable this when we have a solution for running ftrace
// on travis.
TEST_F(PerfettoTest, DISABLED_TestFtraceProducer) {
  base::TestTaskRunner task_runner;
  auto finish = task_runner.CreateCheckpoint("no.more.packets");

  // Setip the TraceConfig for the consumer.
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(4096 * 10);
  trace_config.set_duration_ms(200);

  // Create the buffer for ftrace.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("com.google.perfetto.ftrace");
  ds_config->set_target_buffer(0);

  // Setup the config for ftrace.
  auto* ftrace_config = ds_config->mutable_ftrace_config();
  *ftrace_config->add_event_names() = "sched_switch";
  *ftrace_config->add_event_names() = "bar";

  // Create the function to handle packets as they come in.
  long total = 0;
  auto function = [&total, &finish](std::vector<TracePacket> packets,
                                    bool has_more) {
    if (has_more) {
      for (auto& packet : packets) {
        packet.Decode();
        ASSERT_TRUE(packet->has_ftrace_events());
        for (int ev = 0; ev < packet->ftrace_events().event_size(); ev++) {
          ASSERT_TRUE(packet->ftrace_events().event(ev).has_sched_switch());
        }
      }
      total += packets.size();

      // TODO(lalitm): renable this when stiching inside the service is present.
      // ASSERT_FALSE(packets->empty());
    } else {
      ASSERT_GE(total, sysconf(_SC_NPROCESSORS_CONF));
      ASSERT_TRUE(packets.empty());
      finish();
    }
  };

// If we're building with the Android platform (i.e. CTS), we expect that
// the service and ftrace producer both exist and are already running.
// TODO(lalitm): maybe add an additional build flag for CTS.
#if !defined(PERFETTO_BUILD_WITH_ANDROID)
  TaskRunnerThread<ServiceHandle> service_thread;
  service_thread.Run();

  TaskRunnerThread<FtraceProducerHandle> producer_thread;
  producer_thread.Run();
#endif

  // Finally, make the consumer connect to the service.
  FakeConsumer consumer(trace_config, std::move(function), &task_runner);
  consumer.Connect();

  task_runner.RunUntilCheckpoint("no.more.packets");
}

TEST_F(PerfettoTest, TestFakeProducer) {
  base::TestTaskRunner task_runner;
  auto finish = task_runner.CreateCheckpoint("no.more.packets");

  // Setip the TraceConfig for the consumer.
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(4096 * 10);
  trace_config.set_duration_ms(200);

  // Create the buffer for ftrace.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("android.perfetto.FakeProducer");
  ds_config->set_target_buffer(0);

  // Setup the config for ftrace.
  ds_config->set_trace_category_filters("foo,bar");

  // Create the function to handle packets as they come in.
  long total = 0;
  auto function = [&total, &finish](std::vector<TracePacket> packets,
                                    bool has_more) {
    if (has_more) {
      for (auto& packet : packets) {
        packet.Decode();
        ASSERT_TRUE(packet->has_test());
        ASSERT_EQ(packet->test(), "test");
      }
      total += packets.size();

      // TODO(lalitm): renable this when stiching inside the service is present.
      // ASSERT_FALSE(packets->empty());
    } else {
      ASSERT_EQ(total, 10u);
      ASSERT_TRUE(packets.empty());
      finish();
    }
  };

// If we're building with the Android platform (i.e. CTS), we expect that
// the service and ftrace producer both exist and are already running.
// TODO(lalitm): maybe add an additional build flag for CTS.
#if !defined(PERFETTO_BUILD_WITH_ANDROID)
  TaskRunnerThread<ServiceHandle> service_thread;
  service_thread.Run();

  TaskRunnerThread<FakeProducerHandle> producer_thread;
  producer_thread.Run();
#endif

  // Finally, make the consumer connect to the service.
  FakeConsumer consumer(trace_config, std::move(function), &task_runner);
  consumer.Connect();

  task_runner.RunUntilCheckpoint("no.more.packets");
}

}  // namespace perfetto
