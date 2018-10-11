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
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "perfetto/base/build_config.h"
#include "perfetto/base/event.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/temp_file.h"
#include "perfetto/base/thread_checker.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/base/utils.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"
#include "src/tracing/ipc/default_socket.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <linux/memfd.h>
#include <sys/syscall.h>
#endif

#include "perfetto/config/trace_config.pb.h"

#define PERFETTO_EXPORTED_API extern "C" __attribute__((visibility("default")))

namespace {
using namespace perfetto;

class TracingSession : public Consumer {
 public:
  TracingSession(base::TaskRunner*,
                 PerfettoConsumer_Handle,
                 PerfettoConsumer_OnStateChangedCb,
                 const perfetto::protos::TraceConfig&);
  ~TracingSession() override;

  // Note: if making this class moveable, the move-ctor/dtor must be updated
  // to clear up mapped_buf_ on dtor.

  // These methods are called on a thread != |task_runner_|.
  PerfettoConsumer_State state() const { return state_; }
  std::pair<char*, size_t> mapped_buf() const {
    // The comparison operator will do an acquire-load on the atomic |state_|.
    if (state_ == PerfettoConsumer_kTraceEnded)
      return std::make_pair(mapped_buf_, mapped_buf_size_);
    return std::make_pair(nullptr, 0);
  }

  // All the methods below are called only on the |task_runner_| thread.

  bool Initialize();
  void StartTracing();

  // perfetto::Consumer implementation.
  void OnConnect() override;
  void OnDisconnect() override;
  void OnTracingDisabled() override;
  void OnTraceData(std::vector<TracePacket>, bool has_more) override;

 private:
  TracingSession(const TracingSession&) = delete;
  TracingSession& operator=(const TracingSession&) = delete;

  void DestroyConnection();
  void NotifyCallback();

  base::TaskRunner* const task_runner_;
  PerfettoConsumer_Handle const handle_;
  PerfettoConsumer_OnStateChangedCb const callback_ = nullptr;
  TraceConfig trace_config_;
  base::ScopedFile buf_fd_;
  std::unique_ptr<TracingService::ConsumerEndpoint> consumer_endpoint_;

  // |mapped_buf_| and |mapped_buf_size_| are seq-consistent with |state_|.
  std::atomic<PerfettoConsumer_State> state_{PerfettoConsumer_kIdle};
  char* mapped_buf_ = nullptr;
  size_t mapped_buf_size_ = 0;

  PERFETTO_THREAD_CHECKER(thread_checker_)
};

TracingSession::TracingSession(
    base::TaskRunner* task_runner,
    PerfettoConsumer_Handle handle,
    PerfettoConsumer_OnStateChangedCb callback,
    const perfetto::protos::TraceConfig& trace_config_proto)
    : task_runner_(task_runner), handle_(handle), callback_(callback) {
  PERFETTO_DETACH_FROM_THREAD(thread_checker_);
  trace_config_.FromProto(trace_config_proto);
  trace_config_.set_write_into_file(true);

  // TODO(primiano): this really doesn't matter because the trace will be
  // flushed into the file when stopping. We need a way to be able to say
  // "disable periodic flushing and flush only when stopping".
  trace_config_.set_file_write_period_ms(60000);
}

TracingSession::~TracingSession() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (mapped_buf_)
    PERFETTO_CHECK(munmap(mapped_buf_, mapped_buf_size_) == 0);
}

bool TracingSession::Initialize() {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  if (state_ != PerfettoConsumer_kIdle)
    return false;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  char memfd_name[64];
  snprintf(memfd_name, sizeof(memfd_name), "perfetto_trace_%d", handle_);
  buf_fd_.reset(
      static_cast<int>(syscall(__NR_memfd_create, memfd_name, MFD_CLOEXEC)));
#else
  // Fallback for testing on Linux/mac.
  buf_fd_ = base::TempFile::CreateUnlinked().ReleaseFD();
#endif

  if (!buf_fd_) {
    PERFETTO_PLOG("Failed to allocate temporary tracing buffer");
    return false;
  }

  state_ = PerfettoConsumer_kConnecting;
  consumer_endpoint_ =
      ConsumerIPCClient::Connect(GetConsumerSocket(), this, task_runner_);

  return true;
}

// Called after EnabledTracing, soon after the IPC connection is established.
void TracingSession::OnConnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  PERFETTO_DLOG("OnConnect");
  PERFETTO_DCHECK(state_ == PerfettoConsumer_kConnecting);
  consumer_endpoint_->EnableTracing(trace_config_,
                                    base::ScopedFile(dup(*buf_fd_)));
  if (trace_config_.deferred_start())
    state_ = PerfettoConsumer_kConfigured;
  else
    state_ = PerfettoConsumer_kTracing;
  NotifyCallback();
}

void TracingSession::StartTracing() {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  auto state = state_.load();
  if (state != PerfettoConsumer_kConfigured) {
    PERFETTO_ELOG("StartTracing(): invalid state (%d)", state);
    return;
  }
  state_ = PerfettoConsumer_kTracing;
  consumer_endpoint_->StartTracing();
}

void TracingSession::OnTracingDisabled() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("OnTracingDisabled");

  struct stat stat_buf {};
  int res = fstat(buf_fd_.get(), &stat_buf);
  mapped_buf_size_ = res == 0 ? static_cast<size_t>(stat_buf.st_size) : 0;
  mapped_buf_ =
      static_cast<char*>(mmap(nullptr, mapped_buf_size_, PROT_READ | PROT_WRITE,
                              MAP_SHARED, buf_fd_.get(), 0));
  DestroyConnection();
  if (mapped_buf_size_ == 0 || mapped_buf_ == MAP_FAILED) {
    mapped_buf_ = nullptr;
    mapped_buf_size_ = 0;
    state_ = PerfettoConsumer_kTraceFailed;
    PERFETTO_ELOG("Tracing session failed");
  } else {
    state_ = PerfettoConsumer_kTraceEnded;
  }
  NotifyCallback();
}

void TracingSession::OnDisconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("OnDisconnect");
  DestroyConnection();
  state_ = PerfettoConsumer_kConnectionError;
  NotifyCallback();
}

void TracingSession::DestroyConnection() {
  // Destroys the connection in a separate task. This is to avoid destroying
  // the IPC connection directly from within the IPC callback.
  std::shared_ptr<TracingService::ConsumerEndpoint> endpoint(
      std::move(consumer_endpoint_));
  task_runner_->PostTask([endpoint] {});
}

void TracingSession::OnTraceData(std::vector<TracePacket>, bool) {
  // This should be never called because we are using |write_into_file| and
  // asking the traced service to directly write into the |buf_fd_|.
  PERFETTO_DCHECK(false);
}

void TracingSession::NotifyCallback() {
  if (!callback_)
    return;
  auto callback = callback_;
  auto handle = handle_;
  auto state = state_.load();
  task_runner_->PostTask(
      [callback, handle, state] { callback(handle, state); });
}

class TracingController {
 public:
  static TracingController* GetInstance();
  TracingController();

  // These methods are called from a thread != |task_runner_|.
  PerfettoConsumer_Handle Create(const void*,
                                 size_t,
                                 PerfettoConsumer_OnStateChangedCb);
  void StartTracing(PerfettoConsumer_Handle);
  PerfettoConsumer_State PollState(PerfettoConsumer_Handle);
  PerfettoConsumer_TraceBuffer ReadTrace(PerfettoConsumer_Handle);
  void Destroy(PerfettoConsumer_Handle);

 private:
  void ThreadMain();  // Called on |task_runner_| thread.

  std::mutex mutex_;
  std::thread thread_;
  std::unique_ptr<base::UnixTaskRunner> task_runner_;
  std::condition_variable task_runner_initialized_;
  PerfettoConsumer_Handle last_handle_ = 0;
  std::map<PerfettoConsumer_Handle, std::unique_ptr<TracingSession>> sessions_;
};

TracingController* TracingController::GetInstance() {
  static TracingController* instance = new TracingController();
  return instance;
}

TracingController::TracingController()
    : thread_(&TracingController::ThreadMain, this) {
  std::unique_lock<std::mutex> lock(mutex_);
  task_runner_initialized_.wait(lock, [this] { return !!task_runner_; });
}

void TracingController::ThreadMain() {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    task_runner_.reset(new base::UnixTaskRunner());
  }
  task_runner_initialized_.notify_one();
  task_runner_->Run();
}

PerfettoConsumer_Handle TracingController::Create(
    const void* config_proto_buf,
    size_t config_len,
    PerfettoConsumer_OnStateChangedCb callback) {
  perfetto::protos::TraceConfig config_proto;
  bool parsed = config_proto.ParseFromArray(config_proto_buf,
                                            static_cast<int>(config_len));
  if (!parsed) {
    PERFETTO_ELOG("Failed to decode TraceConfig proto");
    return PerfettoConsumer_kInvalidHandle;
  }

  if (!config_proto.duration_ms()) {
    PERFETTO_ELOG("The trace config must specify a duration");
    return PerfettoConsumer_kInvalidHandle;
  }

  TracingSession* session;
  PerfettoConsumer_Handle handle;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    handle = ++last_handle_;
    session =
        new TracingSession(task_runner_.get(), handle, callback, config_proto);
    sessions_.emplace(handle, std::unique_ptr<TracingSession>(session));
  }

  // Enable the TracingSession on its own thread.
  task_runner_->PostTask([session] { session->Initialize(); });

  return handle;
}

void TracingController::StartTracing(PerfettoConsumer_Handle handle) {
  TracingSession* session;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = sessions_.find(handle);
    if (it == sessions_.end()) {
      PERFETTO_ELOG("StartTracing(): Invalid tracing session handle");
      return;
    };
    session = it->second.get();
  }
  task_runner_->PostTask([session] { session->StartTracing(); });
}

PerfettoConsumer_State TracingController::PollState(
    PerfettoConsumer_Handle handle) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto it = sessions_.find(handle);
  if (it == sessions_.end())
    return PerfettoConsumer_kSessionNotFound;
  return it->second->state();
}

PerfettoConsumer_TraceBuffer TracingController::ReadTrace(
    PerfettoConsumer_Handle handle) {
  PerfettoConsumer_TraceBuffer buf{};

  std::unique_lock<std::mutex> lock(mutex_);
  auto it = sessions_.find(handle);
  if (it == sessions_.end()) {
    PERFETTO_DLOG("Handle invalid");
    buf.state = PerfettoConsumer_kSessionNotFound;
    return buf;
  }

  TracingSession* session = it->second.get();
  buf.state = session->state();
  if (buf.state == PerfettoConsumer_kTraceEnded) {
    std::tie(buf.begin, buf.size) = session->mapped_buf();
    return buf;
  }

  PERFETTO_DLOG("ReadTrace(): called in an unexpected state (%d)", buf.state);
  return buf;
}

void TracingController::Destroy(PerfettoConsumer_Handle handle) {
  // Post an empty task on the task runner to delete the session on its own
  // thread.
  std::shared_ptr<TracingSession> session;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = sessions_.find(handle);
    if (it == sessions_.end())
      return;
    session = std::move(it->second);
    sessions_.erase(it);
  }
  task_runner_->PostTask([session] {});
}

}  // namespace

PERFETTO_EXPORTED_API PerfettoConsumer_Handle
PerfettoConsumer_Create(const void* config_proto,
                        size_t config_len,
                        PerfettoConsumer_OnStateChangedCb callback) {
  return TracingController::GetInstance()->Create(config_proto, config_len,
                                                  callback);
}

PERFETTO_EXPORTED_API
void PerfettoConsumer_StartTracing(PerfettoConsumer_Handle handle) {
  return TracingController::GetInstance()->StartTracing(handle);
}

PERFETTO_EXPORTED_API PerfettoConsumer_State
PerfettoConsumer_PollState(PerfettoConsumer_Handle handle) {
  return TracingController::GetInstance()->PollState(handle);
}

PERFETTO_EXPORTED_API PerfettoConsumer_TraceBuffer
PerfettoConsumer_ReadTrace(PerfettoConsumer_Handle handle) {
  return TracingController::GetInstance()->ReadTrace(handle);
}

PERFETTO_EXPORTED_API void PerfettoConsumer_Destroy(
    PerfettoConsumer_Handle handle) {
  TracingController::GetInstance()->Destroy(handle);
}
