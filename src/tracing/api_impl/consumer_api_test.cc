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

#include <array>

#include "perfetto/base/logging.h"
#include "perfetto/public/consumer_api.h"

#include "perfetto/config/trace_config.pb.h"
#include "perfetto/trace/trace.pb.h"

namespace {
std::string GetConfig(uint32_t duration_ms) {
  perfetto::protos::TraceConfig trace_config;
  trace_config.set_duration_ms(duration_ms);
  trace_config.add_buffers()->set_size_kb(4096);
  trace_config.set_deferred_start(true);
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("linux.ftrace");
  ds_config->mutable_ftrace_config()->add_ftrace_events("sched_switch");
  ds_config->mutable_ftrace_config()->add_ftrace_events(
      "mm_filemap_add_to_page_cache");
  ds_config->mutable_ftrace_config()->add_ftrace_events(
      "mm_filemap_delete_from_page_cache");
  ds_config->set_target_buffer(0);
  return trace_config.SerializeAsString();
}

void DumpTrace(PerfettoConsumer_TraceBuffer buf) {
  perfetto::protos::Trace trace;
  bool parsed = trace.ParseFromArray(buf.begin, static_cast<int>(buf.size));
  if (!parsed) {
    PERFETTO_ELOG("Failed to parse the trace");
    return;
  }

  PERFETTO_LOG("Parsing %d trace packets", trace.packet_size());
  for (const auto& packet : trace.packet()) {
    if (packet.has_ftrace_events()) {
      const auto& bundle = packet.ftrace_events();
      for (const auto& ftrace : bundle.event()) {
        if (ftrace.has_mm_filemap_add_to_page_cache()) {
          const auto& evt = ftrace.mm_filemap_add_to_page_cache();
          PERFETTO_LOG(
              "mm_filemap_add_to_page_cache pfn=%llu, dev=%llu, ino=%llu",
              evt.pfn(), evt.s_dev(), evt.i_ino());
        }
        if (ftrace.has_mm_filemap_delete_from_page_cache()) {
          const auto& evt = ftrace.mm_filemap_delete_from_page_cache();
          PERFETTO_LOG(
              "mm_filemap_delete_from_page_cache pfn=%llu, dev=%llu, ino=%llu",
              evt.pfn(), evt.s_dev(), evt.i_ino());
        }
      }
    }
  }
}

void TestSingleBlocking() {
  std::string cfg = GetConfig(1000);
  auto handle = PerfettoConsumer_EnableTracing(cfg.data(), cfg.size());
  PERFETTO_ILOG("Starting, handle=%d state=%d", handle,
                PerfettoConsumer_PollState(handle));
  sleep(1);
  PerfettoConsumer_StartTracing(handle);
  auto buf = PerfettoConsumer_ReadTrace(handle, 5000);
  PERFETTO_ILOG("Got buf=%p %zu", static_cast<void*>(buf.begin), buf.size);
  DumpTrace(buf);
  PERFETTO_ILOG("Destroying");
  PerfettoConsumer_Destroy(handle);
}

void TestSinglePolling() {
  std::string cfg = GetConfig(1000);

  auto handle = PerfettoConsumer_EnableTracing(cfg.data(), cfg.size());
  PERFETTO_ILOG("Starting, handle=%d state=%d", handle,
                PerfettoConsumer_PollState(handle));

  for (int i = 0; i < 10; i++) {
    auto state = PerfettoConsumer_PollState(handle);
    PERFETTO_ILOG("State=%d", state);
    if (state == PerfettoConsumer_kTraceEnded)
      break;
    sleep(1);
    if (i == 0)
      PerfettoConsumer_StartTracing(handle);
  }

  auto buf = PerfettoConsumer_ReadTrace(handle, 0);
  PERFETTO_ILOG("Got buf=%p %zu", static_cast<void*>(buf.begin), buf.size);
  DumpTrace(buf);
  PERFETTO_ILOG("Destroying");
  PerfettoConsumer_Destroy(handle);
}

void TestManyPolling() {
  std::string cfg = GetConfig(8000);

  std::array<PerfettoConsumer_Handle, 5> handles{};
  fd_set fdset;
  FD_ZERO(&fdset);
  int max_fd = 0;
  for (size_t i = 0; i < handles.size(); i++) {
    auto handle = PerfettoConsumer_EnableTracing(cfg.data(), cfg.size());
    handles[i] = handle;
    max_fd = std::max(max_fd, handle);
    FD_SET(handle, &fdset);
    PERFETTO_ILOG("Creating handle=%d state=%d", handle,
                  PerfettoConsumer_PollState(handle));
  }

  // Wait that all sessions are connected.
  for (bool all_connected = false; !all_connected;) {
    all_connected = true;
    for (size_t i = 0; i < handles.size(); i++) {
      if (PerfettoConsumer_PollState(handles[i]) !=
          PerfettoConsumer_kConfigured) {
        all_connected = false;
      }
    }
  }

  // Start only 3 out of 5 sessions, scattering them with 1 second delay.
  for (size_t i = 0; i < handles.size(); i++) {
    if (i % 2 == 0) {
      PerfettoConsumer_StartTracing(handles[i]);
      sleep(1);
    }
  }

  for (int i = 0; i < 10; i++) {
    auto tmp_set = fdset;
    int ret =
        PERFETTO_EINTR(select(max_fd + 1, &tmp_set, nullptr, nullptr, nullptr));
    PERFETTO_LOG("select(): %d", ret);
    if (ret == 3)
      break;
    sleep(1);
  }

  // Read the trace buffers.
  for (size_t i = 0; i < handles.size(); i++) {
    auto buf = PerfettoConsumer_ReadTrace(handles[i], 0);
    PERFETTO_ILOG("ReadTrace[%zu] buf=%p %zu", i, static_cast<void*>(buf.begin),
                  buf.size);
    if (i % 2 == 0) {
      if (!buf.begin) {
        PERFETTO_ELOG("FAIL: the buffer was supposed to be not empty");
      } else {
        DumpTrace(buf);
      }
    }
  }

  PERFETTO_ILOG("Destroying");
  for (size_t i = 0; i < handles.size(); i++)
    PerfettoConsumer_Destroy(handles[i]);
}
}  // namespace

int main() {
  PERFETTO_LOG("Testing single trace, blocking mode");
  PERFETTO_LOG("=============================================================");
  TestSingleBlocking();
  PERFETTO_LOG("=============================================================");

  PERFETTO_LOG("\n");

  PERFETTO_LOG("Testing single trace, polling mode");
  PERFETTO_LOG("=============================================================");
  TestSinglePolling();
  PERFETTO_LOG("=============================================================");

  PERFETTO_LOG("\n");
  PERFETTO_LOG("Testing concurrent traces, polling mode");
  PERFETTO_LOG("=============================================================");
  TestManyPolling();
  PERFETTO_LOG("=============================================================");

  return 0;
}
