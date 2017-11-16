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

#include "tracing/src/unix_rpc/unix_socket.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <memory>

#include "tracing/src/core/base.h"
#include "tracing/src/core/build_config.h"

namespace perfetto {

namespace {

// MSG_NOSIGNAL is not supported on Mac OS X, but in that case the socket is
// created with SO_NOSIGPIPE (See CreateSocket()).
#if BUILDFLAG(OS_MACOSX)
constexpr int kSockFlags = 0;
#else
constexpr int kSockFlags = MSG_NOSIGNAL;
#endif

// Android takes an int instead of socklen_t for the control buffer size.
#if BUILDFLAG(OS_ANDROID)
using cbuf_len_t = size_t;
#else
using cbuf_len_t = socklen_t;
#endif

bool GetSockAddr(const char* socket_name,
                 sockaddr_un* addr,
                 socklen_t* addr_size) {
  memset(addr, 0, sizeof(sockaddr_un));
  const size_t name_len = strlen(socket_name);
  if (name_len >= sizeof(addr->sun_path))
    return false;
  memcpy(addr->sun_path, socket_name, name_len);
  if (addr->sun_path[0] == '@')
    addr->sun_path[0] = '\0';
  addr->sun_family = AF_UNIX;
  *addr_size = static_cast<socklen_t>(
      __builtin_offsetof(sockaddr_un, sun_path) + name_len + 1);
  return true;
}

}  // namespace

UnixSocket::UnixSocket() = default;

UnixSocket::~UnixSocket() {
  Shutdown();
}

UnixSocket::UnixSocket(UnixSocket&& other) noexcept {
  *this = std::move(other);
}

UnixSocket& UnixSocket::operator=(UnixSocket&& other) noexcept {
  sock_ = other.sock_;
  other.sock_ = -1;
  state_ = other.state_;
  other.state_ = State::DISCONNECTED;
  return *this;
}

bool UnixSocket::CreateSocket() {
  if (is_connected()) {
    Shutdown();
    DCHECK(false);
  }
  state_ = State::DISCONNECTED;
  sock_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock_ < 0)
    return false;
#if BUILDFLAG(OS_MACOSX)
  const int no_sigpipe = 1;
  setsockopt(sock_, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
  int fcntl_res = fcntl(sock_, FD_CLOEXEC);
  DCHECK(fcntl_res == 0);
  return true;
}

bool UnixSocket::Listen(const char* socket_name) {
  if (!CreateSocket())
    return false;

  sockaddr_un addr;
  socklen_t addr_size;
  if (!GetSockAddr(socket_name, &addr, &addr_size))
    return false;

// Android takes an int as 3rd argument of bind() instead of socklen_t.
#if BUILDFLAG(OS_ANDROID)
  const int bind_size = static_cast<int>(addr_size);
#else
  const socklen_t bind_size = addr_size;
#endif

  if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), bind_size)) {
    DPLOG("bind()");
    return false;
  }
  if (listen(sock_, SOMAXCONN)) {
    return false;
    DPLOG("listen()");
  }
  state_ = State::LISTENING;
  return true;
}

bool UnixSocket::Accept(UnixSocket* client_socket) {
  sockaddr_un cli_addr = {};
  socklen_t size = sizeof(cli_addr);
  int cli_sock = HANDLE_EINTR(
      accept(sock_, reinterpret_cast<sockaddr*>(&cli_addr), &size));
  if (cli_sock < 0) {
    DPLOG("accept()");
    return false;
  }
  CHECK(client_socket->sock_ < 0);
  client_socket->sock_ = cli_sock;
  client_socket->state_ = State::CONNECTED;
  return true;
}

bool UnixSocket::Connect(const char* socket_name) {
  if (!CreateSocket())
    return false;
  sockaddr_un addr;
  socklen_t addr_size;
  if (!GetSockAddr(socket_name, &addr, &addr_size))
    return false;
  if (HANDLE_EINTR(
          connect(sock_, reinterpret_cast<sockaddr*>(&addr), addr_size))) {
    return false;
  }
  state_ = State::CONNECTED;
  return true;
}

void UnixSocket::Shutdown() {
  if (sock_ >= 0) {
    shutdown(sock_, SHUT_RDWR);
    close(sock_);
  }
  state_ = State::DISCONNECTED;
  sock_ = -1;
}

bool UnixSocket::Send(const void* msg,
                      size_t msg_size,
                      const int* fds,
                      uint32_t fds_size) {
  msghdr msg_hdr = {};
  iovec iov = {const_cast<void*>(msg), msg_size};
  msg_hdr.msg_iov = &iov;
  msg_hdr.msg_iovlen = 1;
  alignas(cmsghdr) char control_buf[256];

  if (fds_size > 0) {
    const cbuf_len_t control_buf_len =
        static_cast<cbuf_len_t>(CMSG_SPACE(fds_size * sizeof(int)));
    CHECK(control_buf_len <= sizeof(control_buf));
    memset(control_buf, 0, sizeof(control_buf));
    msg_hdr.msg_control = control_buf;
    msg_hdr.msg_controllen = control_buf_len;
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg_hdr);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds_size);
    memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * fds_size);
    msg_hdr.msg_controllen = cmsg->cmsg_len;
  }

  const ssize_t sz = HANDLE_EINTR(sendmsg(sock_, &msg_hdr, kSockFlags));
  return sz == static_cast<ssize_t>(msg_size);
}

ssize_t UnixSocket::Recv(void* msg,
                         size_t msg_size,
                         int* fds,
                         uint32_t* fds_size) {
  msghdr msg_hdr = {};
  iovec iov = {msg, msg_size};
  msg_hdr.msg_iov = &iov;
  msg_hdr.msg_iovlen = 1;
  alignas(cmsghdr) char control_buf[256];

  if (fds && fds_size && *fds_size > 0) {
    msg_hdr.msg_control = control_buf;
    msg_hdr.msg_controllen =
        static_cast<cbuf_len_t>(CMSG_SPACE(*fds_size * sizeof(int)));
    CHECK(msg_hdr.msg_controllen <= sizeof(control_buf));
  }

  const ssize_t sz = HANDLE_EINTR(recvmsg(sock_, &msg_hdr, kSockFlags));
  if (sz == -1)
    return -1;

  int* wire_fds = nullptr;
  uint32_t wire_fds_len = 0;

  if (msg_hdr.msg_controllen > 0) {
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg_hdr); cmsg;
         cmsg = CMSG_NXTHDR(&msg_hdr, cmsg)) {
      const size_t payload_len = cmsg->cmsg_len - CMSG_LEN(0);
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        DCHECK(payload_len % sizeof(int) == 0u);
        DCHECK(wire_fds == nullptr);
        wire_fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
        wire_fds_len = static_cast<uint32_t>(payload_len / sizeof(int));
      }
    }
  }

  if (msg_hdr.msg_flags & MSG_TRUNC || msg_hdr.msg_flags & MSG_CTRUNC) {
    for (size_t i = 0; i < wire_fds_len; ++i)
      close(wire_fds[i]);
    errno = EMSGSIZE;
    return -1;
  }

  for (size_t i = 0; wire_fds && i < wire_fds_len; ++i) {
    if (i < *fds_size)
      fds[i] = wire_fds[i];
    else
      close(wire_fds[i]);
  }
  if (fds_size)
    *fds_size = std::min(*fds_size, wire_fds_len);

  return sz;
}

std::string UnixSocket::RecvString(size_t max_length) {
  std::unique_ptr<char[]> buf(new char[max_length + 1]);
  ssize_t rsize = Recv(buf.get(), max_length);
  if (rsize <= 0)
    return std::string();
  CHECK(static_cast<size_t>(rsize) <= max_length);
  buf[static_cast<size_t>(rsize)] = '\0';
  return std::string(buf.get());
}

}  // namespace perfetto
