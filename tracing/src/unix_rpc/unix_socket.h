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

#ifndef TRACING_SRC_POSIX_IPC_POSIX_SOCKET_H_
#define TRACING_SRC_POSIX_IPC_POSIX_SOCKET_H_

#include <stdint.h>
#include <sys/types.h>

#include <string>

namespace perfetto {

class UnixSocket {
 public:
  enum class State {
    DISCONNECTED = 0,
    CONNECTED,
    LISTENING  // Only for service sockets, after Listen()
  };
  UnixSocket();
  ~UnixSocket();

  // Move operators.
  UnixSocket(UnixSocket&&) noexcept;
  UnixSocket& operator=(UnixSocket&&) noexcept;

  // Creates a Unix socket and starts listening or connects. If |socket_name|
  // starts with a '@', an abstract socket will be created (Linux/Android only).
  // Returns false in on failure.
  bool Listen(const char* socket_name);
  bool Connect(const char* socket_name);

  // Accept a new connection after a Listen() call. |client_socket| must be an
  // uninitialized socket.
  bool Accept(UnixSocket* client_socket);

  void Shutdown();

  // |fds| is an optional array of |fds_size| file descriptors that will be
  // transferred to the peer using SCM_RIGHTS control.
  // Returns true if successful.
  bool Send(const void* msg,
            size_t msg_size,
            const int* fds = nullptr,
            uint32_t fds_size = 0);

  // Helper for the above. Does NOT send the null terminator.
  bool Send(const std::string& msg) { return Send(msg.data(), msg.size()); }

  // If the optional |fds| array is non-nullptr, it is filled with received
  // file descriptors up to a max of |fds_size|. Returns the number of bytes
  // written into |msg| (up to |msg_size|) or -1 on failure.
  ssize_t Recv(void* msg,
               size_t msg_size,
               int* fds = nullptr,
               uint32_t* fds_size = nullptr);

  // Mostly for tests and slow paths. This is slower than Recv() as it requires
  // a heap allocation and a copy for the std::string. Guarantees that the
  // returned string is null terminated even if the underlying message sent by
  // the peer is not.
  std::string RecvString(size_t max_length = 1024);

  bool is_connected() const { return state_ == State::CONNECTED; }

  bool is_listening() const { return state_ == State::LISTENING; }

  int fd() const { return sock_; }

 private:
  UnixSocket(const UnixSocket&) = delete;
  UnixSocket& operator=(const UnixSocket&) = delete;
  bool CreateSocket();

  int sock_ = -1;  // TODO(primiano): introduce ScopedFD in base.h
  State state_ = State::DISCONNECTED;
};

}  // namespace perfetto

#endif  // TRACING_SRC_POSIX_IPC_POSIX_SOCKET_H_
