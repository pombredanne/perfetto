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

#include "src/profiling/memory/socket_listener.h"

namespace perfetto {

void SocketListener::OnDisconnect(ipc::UnixSocket* self) {
  sockets_.erase(self);
}

void SocketListener::OnNewIncomingConnection(
    ipc::UnixSocket* self,
    std::unique_ptr<ipc::UnixSocket> new_connection) {
  sockets_.emplace(
      std::piecewise_construct, std::forward_as_tuple(self),
      std::forward_as_tuple(
          std::move(new_connection),
          // This does not need a WeakPtr because
          // it gets called inline of the Read
          // call, which is called in
          // OnDataAvailable below.
          [this, self](size_t size, std::unique_ptr<uint8_t[]> buf) {
            RecordReceived(self, size, std::move(buf));
          }));
}

void SocketListener::OnDataAvailable(ipc::UnixSocket* self) {
  auto it = sockets_.find(self);
  PERFETTO_DCHECK(it != sockets_.end());
  it->second.record_reader.Read(self);
}

void SocketListener::RecordReceived(ipc::UnixSocket* self,
                                    size_t,
                                    std::unique_ptr<uint8_t[]>) {
  auto it = sockets_.find(self);
  PERFETTO_CHECK(it != sockets_.end());
  Entry& entry = it->second;
  // TODO(fmayer): actually do something with this.
  printf("%p\n", static_cast<void*>(entry.sock.get()));
}

}  // namespace perfetto
