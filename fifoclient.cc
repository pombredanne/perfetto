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

#include <string>
#include <tuple>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "perfetto/base/logging.h"

static int tbfd;
static int depth;

#define USE_SPLICE 1

namespace {
class StringSplitter {
 public:
  // Can take ownership of the string if passed via std::move(), e.g.:
  // StringSplitter(std::move(str), '\n');
  StringSplitter(std::string, char delimiter);

  // Returns true if a token is found (in which case it will be stored in
  // cur_token()), false if no more tokens are found.
  bool Next();

  // Returns the current token iff last call to Next() returned true. In this
  // case it guarantees that the returned string is always null terminated.
  // In all other cases (before the 1st call to Next() and after Next() returns
  // false) returns nullptr.
  char* cur_token() { return cur_; }

 private:
  StringSplitter(const StringSplitter&) = delete;
  StringSplitter& operator=(const StringSplitter&) = delete;
  void Initialize(char* str, size_t size);

  std::string str_;
  char* cur_;
  size_t cur_size_;
  char* next_;
  char* end_;  // STL-style, points one past the last char.
  const char delimiter_;
};

StringSplitter::StringSplitter(std::string str, char delimiter)
    : str_(std::move(str)), delimiter_(delimiter) {
  // It's legal to access str[str.size()] in C++11 (it always returns \0),
  // hence the +1 (which becomes just size() after the -1 in Initialize()).
  Initialize(&str_[0], str_.size() + 1);
}

void StringSplitter::Initialize(char* str, size_t size) {
  PERFETTO_DCHECK(!size || str);
  next_ = str;
  end_ = str + size;
  cur_ = nullptr;
  cur_size_ = 0;
  if (size)
    next_[size - 1] = '\0';
}

bool StringSplitter::Next() {
  for (; next_ < end_; next_++) {
    if (*next_ == delimiter_)
      continue;
    cur_ = next_;
    for (;; next_++) {
      if (*next_ == delimiter_) {
        cur_size_ = static_cast<size_t>(next_ - cur_);
        *(next_++) = '\0';
        break;
      }
      if (*next_ == '\0') {
        cur_size_ = static_cast<size_t>(next_ - cur_);
        next_ = end_;
        break;
      }
    }
    if (*cur_)
      return true;
    break;
  }
  cur_ = nullptr;
  cur_size_ = 0;
  return false;
}

constexpr size_t kBufSize = 2048;

bool ReadFile(const std::string& path, std::string* out) {
  // Do not override existing data in string.
  size_t i = out->size();

  int fd = open(path.c_str(), O_RDONLY);
  if (!fd)
    return false;

  struct stat buf {};
  if (fstat(fd, &buf) != -1) {
    if (buf.st_size > 0)
      out->resize(i + static_cast<size_t>(buf.st_size));
  }

  ssize_t bytes_read;
  for (;;) {
    if (out->size() < i + kBufSize)
      out->resize(out->size() + kBufSize);

    bytes_read = PERFETTO_EINTR(read(fd, &((*out)[i]), kBufSize));
    if (bytes_read > 0) {
      i += static_cast<size_t>(bytes_read);
    } else {
      out->resize(i);
      return bytes_read == 0;
    }
  }
}

std::pair<void*, void*> FindStack() {
  std::string maps;
  PERFETTO_CHECK(ReadFile("/proc/self/maps", &maps));
  std::string pointers;
  std::string name;
  for (StringSplitter ss(std::move(maps), '\n'); ss.Next();) {
    const char* line = ss.cur_token();
    int i = 0;
    for (StringSplitter ls(line, ' '); ls.Next();) {
      if (i == 0)
        pointers = ls.cur_token();
      if (i == 5)
        name = ls.cur_token();
      ++i;
    }
    if (name == "[stack]")
      break;
  }

  PERFETTO_CHECK(name == "[stack]");
  char* p;
  void* start = reinterpret_cast<void*>(strtoll(pointers.c_str(), &p, 16));
  PERFETTO_CHECK(*p == '-');
  char* p2;
  void* end = reinterpret_cast<void*>(strtoll(p + 1, &p2, 16));
  return {start, end};
}

std::pair<void*, void*> GetStack() {
  static std::pair<void*, void*> stack = FindStack();
  return stack;
}

void SendStack() {
  clock_t t = clock();
  auto stackbounds = GetStack();
  void* sp = __builtin_frame_address(0);
#ifdef PAGE_ALIGN
  sp = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(sp) / 4096) * 4096);
#endif
  //  sp = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(sp) /
  //  base::kPageSize) * base::kPageSize);
  size_t size = reinterpret_cast<uintptr_t>(stackbounds.second) -
                reinterpret_cast<uintptr_t>(sp);
#ifdef USE_SPLICE
  // ~7-15 ticks.
  struct iovec v[1];
  v[0].iov_base = sp;
  v[0].iov_len = size;
  PERFETTO_CHECK(vmsplice(tbfd, v, 1, 0) != -1);
#else
  // ~30-40 ticks.
  PERFETTO_CHECK(write(tbfd, sp, size) == static_cast<ssize_t>(size));
#endif
  t = clock() - t;
  printf("%s,%d,%ld,%zd\n", INSTANCE_NAME, depth, t, size);
}

}  // namespace

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
using CBufLenType = size_t;
#else
using CBufLenType = socklen_t;
#endif

int rec(int n);
int rec(int n) {
  if (n == 0) {
    SendStack();
    return 1;
  }
  return n + rec(n - 1);
}

size_t Receive(int sock, void* msg, size_t len, int* recv_fd);
size_t Receive(int sock, void* msg, size_t len, int* recv_fd) {
  msghdr msg_hdr = {};
  iovec iov = {msg, len};
  msg_hdr.msg_iov = &iov;
  msg_hdr.msg_iovlen = 1;
  alignas(cmsghdr) char control_buf[256];

  if (recv_fd) {
    msg_hdr.msg_control = control_buf;
    msg_hdr.msg_controllen = static_cast<CBufLenType>(CMSG_SPACE(sizeof(int)));
    PERFETTO_CHECK(msg_hdr.msg_controllen <= sizeof(control_buf));
  }
  const ssize_t sz = PERFETTO_EINTR(recvmsg(sock, &msg_hdr, 0));
  if (sz < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return 0;
  }
  if (sz < 0) {
    PERFETTO_CHECK(false);
    return 0;
  }
  PERFETTO_CHECK(static_cast<size_t>(sz) <= len);

  int* fds = nullptr;
  uint32_t fds_len = 0;

  if (msg_hdr.msg_controllen > 0) {
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg_hdr); cmsg;
         cmsg = CMSG_NXTHDR(&msg_hdr, cmsg)) {
      const size_t payload_len = cmsg->cmsg_len - CMSG_LEN(0);
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        PERFETTO_DCHECK(payload_len % sizeof(int) == 0u);
        PERFETTO_DCHECK(fds == nullptr);
        fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
        fds_len = static_cast<uint32_t>(payload_len / sizeof(int));
      }
    }
  }

  if (msg_hdr.msg_flags & MSG_TRUNC || msg_hdr.msg_flags & MSG_CTRUNC) {
    for (size_t i = 0; fds && i < fds_len; ++i)
      close(fds[i]);
    PERFETTO_CHECK(false);
    return 0;
  }

  for (size_t i = 0; fds && i < fds_len; ++i) {
    if (recv_fd && i == 0) {
      *recv_fd = fds[i];
    } else {
      close(fds[i]);
    }
  }

  return static_cast<size_t>(sz);
}

int main(int argc, char** argv) {
  PERFETTO_CHECK(argc == 3);

  depth = atoi(argv[2]);

  // Warm stack cache.
  GetStack();
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  PERFETTO_CHECK(sock != -1);
  struct sockaddr_un name;
  name.sun_family = AF_UNIX;
  strncpy(name.sun_path, argv[1], sizeof(name.sun_path) - 1);
  PERFETTO_CHECK(connect(sock, reinterpret_cast<const struct sockaddr*>(&name),
                         sizeof(name)) != -1);
  while (!tbfd) {
    char buf[256];
    Receive(sock, buf, sizeof(buf), &tbfd);
  }
  rec(depth);
  return 0;
}
