/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "tracing/src/unix_rpc/unix_producer_proxy.h"

#include <inttypes.h>
#include <stdio.h>

#include "tracing/core/data_source_config.h"
#include "tracing/core/data_source_descriptor.h"
#include "tracing/core/service.h"
#include "tracing/core/task_runner.h"
#include "tracing/src/core/base.h"
#include "tracing/src/unix_rpc/unix_shared_memory.h"

namespace perfetto {

UnixProducerProxy::UnixProducerProxy(
    UnixSocket conn,
    TaskRunner* task_runner,
    UnixServiceHost::ObserverForTesting* observer)
    : conn_(std::move(conn)), task_runner_(task_runner), observer_(observer) {
  DCHECK(conn_.is_connected());
  task_runner_->AddFileDescriptorWatch(
      conn_.fd(), std::bind(&UnixProducerProxy::OnDataAvailable, this));
}

UnixProducerProxy::~UnixProducerProxy() {}

void UnixProducerProxy::OnDataAvailable() {
  char cmd[1024];
  ssize_t rsize = conn_.Recv(cmd, sizeof(cmd) - 1);
  if (rsize <= 0) {
    task_runner_->RemoveFileDescriptorWatch(conn_.fd());
    return;  // TODO connection closed (very likely peer died. do something.
  }

  cmd[rsize] = '\0';

  // TODO if we home brew the RPC protocol, each message should be prefixed by
  // a counter (think to IMAP protocol), so that we can match requests and
  // replies. For instance, if we get two RegisterDataSource requests back to
  // back, how does the client know which reply is which? Either we just rely
  // on FIFO (maybe okay given this is a SOCK_STREAM socket) or we do something
  // like:
  //     -> 10 RegisterDataSource Foo
  //     -> 20 RegisterDataSource Bar
  //     <- 10 RegisterDataSourceReply FooID
  //     <- 20 RegisterDataSourceReply BarID
  // For the moment not going to bother and just assuming FIFO ordering.

  // Also all these payloads here should really be protobuf. But for the sake of
  // sketching this is going to be just a poor man's RPC.

  char data_source_name[64];
  if (sscanf(cmd, "RegisterDataSource %64s", data_source_name) == 1) {
    DataSourceDescriptor descriptor{std::string(data_source_name)};
    auto callback = [this](DataSourceID dsid) {
      // TODO what to do if the send fails (conn closed?) here and below?
      conn_.Send("RegisterDataSourceCallback " + std::to_string(dsid));
      task_runner_->PostTask(std::bind(
          &UnixServiceHost::ObserverForTesting::OnDataSourceRegistered,
          observer_, dsid));
    };

    // TODO lifetime: what happens if the producer is destroyed between soon
    // after this call, before the callback is invoked?
    svc_->RegisterDataSource(descriptor, callback);
    return;
  }

  uint32_t page_index;
  if (sscanf(cmd, "NotifyPageAcquired %" SCNu32, &page_index) == 1) {
    svc_->NotifyPageAcquired(page_index);
    return;
  }

  if (sscanf(cmd, "NotifyPageReleased %" SCNu32, &page_index) == 1) {
    svc_->NotifyPageReleased(page_index);
    return;
  }

  DLOG("Received unknown RPC from producer: \"%s\"", cmd);
  DCHECK(false);
}

void UnixProducerProxy::OnConnect(ProducerID prid, SharedMemory* shm) {
  char msg[32];
  sprintf(msg, "OnConnect %" PRIu64, prid);
  int fd = static_cast<const UnixSharedMemory*>(shm)->fd();
  bool res = conn_.Send(msg, strlen(msg), &fd, 1);
  DCHECK(res);
}

void UnixProducerProxy::CreateDataSourceInstance(
    DataSourceInstanceID instance_id,
    const DataSourceConfig& config) {
  conn_.Send("CreateDataSourceInstance " + std::to_string(instance_id) + " " +
             config.data_source_name + " " + config.trace_category_filters);
}

void UnixProducerProxy::TearDownDataSourceInstance(
    DataSourceInstanceID instance_id) {
  conn_.Send("TearDownDataSourceInstance " + std::to_string(instance_id));
}

}  // namespace perfetto
