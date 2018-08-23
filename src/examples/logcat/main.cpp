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
//

#include "perfetto/base/build_config.h"  // PERFETTO_OS_ANDROID
#include "perfetto/tracing/core/consumer.h"
// TODO: this should come from consumer.h
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"

#include "perfetto/trace/ftrace/sched_switch.pb.h"
#include "perfetto/trace/trace_packet.pb.h"
#include "perfetto/trace/ftrace/ftrace_event_bundle.pb.h"
#include "perfetto/trace/ftrace/ftrace_event.pb.h"

// TODO: it would be nice if this was public.
// #include "perfetto/tracing/ipc/default_socket.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include "perfetto/base/android_task_runner.h"
#endif

#include <iostream>
#include <inttypes.h>
#include <time.h>

#define EXAMPLE_LOG(fmt, ...) /*fprintf(stderr, fmt, ##__VA_ARGS__);*/ PERFETTO_LOG(fmt, ##__VA_ARGS__)

using namespace perfetto;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
using PlatformTaskRunner = base::AndroidTaskRunner;
#else
using PlatformTaskRunner = base::UnixTaskRunner;
#endif

static const char* GetConsumerSocket() {
  const char* name;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  name = "/dev/socket/traced_consumer";
#else
  name = "/tmp/perfetto-consumer";
#endif
  return name;
}

class ExampleConsumer : public Consumer {
  ExampleConsumer() {
  }
 public:

  virtual ~ExampleConsumer() {}

  static std::unique_ptr<ExampleConsumer> Create() {
    std::unique_ptr<ExampleConsumer> self{new ExampleConsumer{}};

    if (!self->Connect()) {
      return nullptr;
    }

    return self;
  }

  // Called by Service (or more typically by the transport layer, on behalf of
  // the remote Service), once the Consumer <> Service connection has been
  // established.
  virtual void OnConnect() {
    EXAMPLE_LOG("OnConnect");

    // see e.g. test/configs/ftrace.cfg for some valid values

    TraceConfig trace_config;
    TraceConfig::DataSource& data_source = *trace_config.add_data_sources();
    TraceConfig::BufferConfig& buffer_config = *trace_config.add_buffers();
    buffer_config.set_size_kb(10024);
    // uses default fill policy = UNSPECIFIED = RING_BUFFER
    // buffer_config.set_fill_policy(RING_BUFFER);

    DataSourceConfig& mutable_config = *data_source.mutable_config();
    mutable_config.set_name("linux.ftrace");  // magical
    mutable_config.set_target_buffer(/*value*/0);  // index into the buffers config

    FtraceConfig& ftrace_config = *mutable_config.mutable_ftrace_config();
    std::string& ftrace_event = *ftrace_config.add_ftrace_events();


    // TODO: should run 'forever' until we cancel.
    trace_config.set_duration_ms(2000);

    ftrace_event = "sched_switch";

    consumer_endpoint_->EnableTracing(trace_config);
  }

  // Called by the Service or by the transport layer if the connection with the
  // service drops, either voluntarily (e.g., by destroying the ConsumerEndpoint
  // obtained through Service::ConnectConsumer()) or involuntarily (e.g., if the
  // Service process crashes).
  virtual void OnDisconnect() {
    EXAMPLE_LOG("OnDisconnect");
    // Should only need this if the server-side dies.
    task_runner_.Quit();
  }

  // Called by the Service after the tracing session has ended. This can happen
  // for a variety of reasons:
  // - The consumer explicitly called DisableTracing()
  // - The TraceConfig's |duration_ms| has been reached.
  // - The TraceConfig's |max_file_size_bytes| has been reached.
  // - An error occurred while trying to enable tracing.
  virtual void OnTracingDisabled() {
    EXAMPLE_LOG("OnTracingDisabled");

    // Cause the OnTraceData callback to give us back all the data.
    consumer_endpoint_->ReadBuffers();
  }

  // Called back by the Service (or transport layer) after invoking
  // TracingService::ConsumerEndpoint::ReadBuffers(). This function can be
  // called more than once. Each invocation can carry one or more
  // TracePacket(s). Upon the last call, |has_more| is set to true (i.e.
  // |has_more| is a !EOF).
  virtual void OnTraceData(std::vector<TracePacket> packets, bool has_more) {
    EXAMPLE_LOG("OnTraceData size: %zu, has_more: %d",
                 packets.size(), static_cast<int>(has_more));

    for (TracePacket& packet : packets) {
      DecodePacket(packet);
    }

    if (!has_more) {
      // Terminate the runner, breaking out of the main() function and exiting.
      EXAMPLE_LOG("Requesting Quit");
      task_runner_.Quit();
    }
  }

  void Run() {
    // Block indefinitely while tasks are processed.
    task_runner_.Run();
  }

 private:
  void DecodePacket(const TracePacket& packet) {
    protos::TracePacket proto_packet;
    if (!packet.Decode(/*out*/&proto_packet)) {
      EXAMPLE_LOG("Decode packet failed");
      return;
    }

    if (proto_packet.has_ftrace_events()) {
      const protos::FtraceEventBundle& ftrace_events = proto_packet.ftrace_events();

      for (size_t i = 0; i < ftrace_events.event_size(); ++i) {
        const protos::FtraceEvent& ftrace_event = ftrace_events.event(i);


        protos::FtraceEvent::EventCase event_case = ftrace_event.event_case();

        // Also has_sched_switch() works.
        if (event_case == protos::FtraceEvent::kSchedSwitch) {
          const protos::SchedSwitchFtraceEvent& sched_switch = ftrace_event.sched_switch();

          EXAMPLE_LOG("  sched_switch_ftrace_event:");

          if (sched_switch.has_prev_comm()) {
            EXAMPLE_LOG("    prev_comm: %s", sched_switch.prev_comm().c_str());
          }
          if (sched_switch.has_next_comm()) {
            EXAMPLE_LOG("    next_comm: %s", sched_switch.next_comm().c_str());
          }

        }

        if (ftrace_event.has_timestamp()) {
          EXAMPLE_LOG("    timestamp: %" PRIu64, static_cast<uint64_t>(ftrace_event.timestamp()));
        }
      }

    }
  }

  bool Connect() {
    consumer_endpoint_ =
        ConsumerIPCClient::Connect(GetConsumerSocket(), this, &task_runner_);

    return (consumer_endpoint_ != nullptr);
  }

  PlatformTaskRunner task_runner_;
  std::unique_ptr<TracingService::ConsumerEndpoint> consumer_endpoint_;
};

int main() {
  EXAMPLE_LOG("Creating consumer...");

  struct timespec tp{};
  clock_gettime(CLOCK_BOOTTIME, &tp);
  uint64_t nano_time = tp.tv_sec * 1000000000LL + tp.tv_nsec;

  EXAMPLE_LOG("The time is now %" PRIu64, nano_time);

  std::unique_ptr<ExampleConsumer> example_consumer =
      ExampleConsumer::Create();

  // Block forever, processing all tasks.
  example_consumer->Run();

  /*
  // Block until something is typed in.
  int x;
  std::cin >> x;
  (void)x;
  */

  return 0;
}
