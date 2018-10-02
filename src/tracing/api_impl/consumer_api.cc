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

#include "perfetto/public/consumer_api.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <thread>

#include "perfetto/base/build_config.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"
#include "src/tracing/ipc/default_socket.h"

#include "perfetto/config/trace_config.pb.h"

#define EXPORTED_API extern "C" __attribute__((visibility("default")))

// TODO disable TaskRunner's watchdog.

namespace {
using namespace perfetto;

class ConsumerImpl : public Consumer {
 public:
  static ConsumerImpl* GetInstance();

  base::ScopedFile EnableTracing(const void* trace_config_proto,
                                 size_t trace_config_len);
  void StartTracing();

  // perfetto::Consumer implementation.
  void OnConnect() override;
  void OnDisconnect() override;
  void OnTracingDisabled() override;
  void OnTraceData(std::vector<TracePacket>, bool has_more) override;

  // PerfettoConsumer_State state() const { return state_; }

 private:
  void ReadTraceDataIntoPipe();

  base::ScopedFile pipe_wr_;
  PerfettoConsumer_State state_ = PerfettoConsumer_kDisconnected;
  base::UnixTaskRunner task_runner_;
  std::unique_ptr<perfetto::TracingService::ConsumerEndpoint>
      consumer_endpoint_;
  TraceConfig trace_config_;
};

// static
ConsumerImpl* ConsumerImpl::GetInstance() {
  static ConsumerImpl* instance = new ConsumerImpl();
  return instance;
}

// TODO this happens on another thread.
base::ScopedFile ConsumerImpl::EnableTracing(const void* trace_config_raw,
                                             size_t trace_config_len) {
  base::ScopedFile pipe_rd;
  if (state_ != PerfettoConsumer_kDisconnected)
    return pipe_rd;

  perfetto::protos::TraceConfig trace_config_proto;
  bool parsed = trace_config_proto.ParseFromArray(
      trace_config_raw, static_cast<int>(trace_config_len));
  if (!parsed) {
    PERFETTO_ELOG("Failed to decode TraceConfig proto");
    return pipe_rd;
  }

  int fds[2]{};
  if (pipe(fds) != 0) {
    PERFETTO_PLOG("pipe() failed");
    return pipe_rd;
  }

  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  pipe_rd.reset(fds[0]);
  pipe_wr_.reset(fds[1]);

  signal(SIGPIPE, SIG_IGN);  // TODO document.

  consumer_endpoint_ =
      ConsumerIPCClient::Connect(GetConsumerSocket(), this, &task_runner_);

  return pipe_rd;
}

void ConsumerImpl::StartTracing() {
  if (state_ != PerfettoConsumer_kEnabled) {
    PERFETTO_ELOG("StartTracing() called but consumer is not connected");
    return;
  }
  consumer_endpoint_->StartTracing();
}

void ConsumerImpl::OnConnect() {
  PERFETTO_LOG("Connected");
  state_ = PerfettoConsumer_kEnabled;
  consumer_endpoint_->EnableTracing(trace_config_);
  task_runner_.PostTask([this] { ReadTraceDataIntoPipe(); });
}

void ConsumerImpl::ReadTraceDataIntoPipe() {
  consumer_endpoint_->ReadBuffers();
}

void ConsumerImpl::OnTraceData(std::vector<TracePacket> packets,
                               bool has_more) {
  std::vector<struct iovec> iovecs;
  for (const auto& packet : packets) {
    for (const auto& slice : packet.slices()) {
      iovecs.emplace_back(iovec{const_cast<void*>(slice.start), slice.size});
    }
  }

  // TODO(primiano): use fmayer's unshift to deal with partial writes.
  auto wsize = writev(*pipe_wr_, &iovecs[0], static_cast<int>(iovecs.size()));
  if (wsize < 0) {
    if (errno == EPIPE) {
      consumer_endpoint_.reset();
    } else {
      PERFETTO_PLOG("writev(pipe) failed");
    }
    return;
  }

  if (!has_more) {
    pipe_wr_.reset();
    consumer_endpoint_.reset();  // In turn this will call OnDisconnect();
    return;
  }

  task_runner_.PostTask([this] { ReadTraceDataIntoPipe(); });
}

void ConsumerImpl::OnTracingDisabled() {
  PERFETTO_LOG("Disabled");
  state_ = PerfettoConsumer_kDisconnected;
  pipe_wr_.reset();
  consumer_endpoint_.reset();
}

void ConsumerImpl::OnDisconnect() {
  state_ = PerfettoConsumer_kDisconnected;
  PERFETTO_LOG("Disconnected");
  pipe_wr_.reset();
  task_runner_.Quit();
}

}  // namespace

EXPORTED_API int PerfettoConsumer_EnableTracing(const void* trace_config_proto,
                                                size_t trace_config_len) {
  // TODO mutex.

  perfetto::base::ScopedFile pipe_rd =
      ConsumerImpl::GetInstance()->EnableTracing(trace_config_proto,
                                                 trace_config_len);
  return pipe_rd.release();
}

EXPORTED_API void PerfettoConsumer_StartTracing() {
  ConsumerImpl::GetInstance()->StartTracing();
}
