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

#include "test/fake_consumer.h"

#include "perfetto/base/unix_task_runner.h"
#include "perfetto/traced/traced.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"
#include "perfetto/tracing/ipc/service_ipc_host.h"

#include "protos/trace_packet.pb.h"
#include "protos/trace_packet.pbzero.h"

#include "src/traced/probes/ftrace_producer.h"

namespace perfetto {

TEST(PerfettoTest, TestFtraceProducer) {
  base::TestTaskRunner task_runner;
  auto finish = task_runner.CreateCheckpoint("no.more.packets");

  // Setip the TraceConfig for the consumer.
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(4096 * 10);
  trace_config.set_duration_ms(1000);

  // Create the buffer for ftrace.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("com.google.perfetto.ftrace");
  ds_config->set_target_buffer(1);

  // Setup the config for ftrace.
  auto* ftrace_config = ds_config->mutable_ftrace_config();
  *ftrace_config->add_event_names() = "sched_switch";
  *ftrace_config->add_event_names() = "bar";

  // Create the function to handle packets as they come in.
  uint64_t total = 0;
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
      ASSERT_GE(total, 8u);
      ASSERT_TRUE(packets.empty());
      finish();
    }
  };

// If we're building with the Android platform (i.e. CTS), we expect that
// the service and ftrace producer both exist and are already running.
// TODO(lalitm): maybe add an additional build flag for CTS.
#if !defined(PERFETTO_BUILD_WITH_ANDROID)
  // Create the service.
  std::unique_ptr<ServiceIPCHost> svc;
  svc = ServiceIPCHost::CreateInstance(&task_runner);
  unlink(PERFETTO_PRODUCER_SOCK_NAME);
  unlink(PERFETTO_CONSUMER_SOCK_NAME);
  svc->Start(PERFETTO_PRODUCER_SOCK_NAME, PERFETTO_CONSUMER_SOCK_NAME);

  // Create the ftrace producer.
  FtraceProducer producer;
  producer.Connect(&task_runner);
#endif

  // Finally, make the consumer connect to the service.
  FakeConsumer consumer(trace_config, std::move(function), &task_runner);
  consumer.Connect();

  task_runner.RunUntilCheckpoint("no.more.packets");
}

}  // namespace perfetto
