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

#include <fcntl.h>
#include <inttypes.h>
#include <linux/memfd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <map>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/base/weak_ptr.h"
#include "src/ipc/unix_socket.h"

namespace perfetto {

static long total_read = 0;

class PipeSender : public ipc::UnixSocket::EventListener {
 public:
  PipeSender(base::TaskRunner* task_runner)
      : task_runner_(task_runner), weak_factory_(this) {}

  void OnNewIncomingConnection(ipc::UnixSocket*,
                               std::unique_ptr<ipc::UnixSocket>) override;
  void OnDisconnect(ipc::UnixSocket* self) override;
  void OnDataAvailable(ipc::UnixSocket* sock) override {
    char buf[4096];
    sock->Receive(&buf, sizeof(buf));
  }

 private:
  base::TaskRunner* task_runner_;
  base::WeakPtrFactory<PipeSender> weak_factory_;
  std::map<ipc::UnixSocket*, std::unique_ptr<ipc::UnixSocket>> socks_;
};

void PipeSender::OnNewIncomingConnection(
    ipc::UnixSocket*,
    std::unique_ptr<ipc::UnixSocket> new_connection) {
  int pipes[2];
  PERFETTO_CHECK(pipe(pipes) != -1);
  new_connection->Send("x", 1, pipes[1]);
  ipc::UnixSocket* p = new_connection.get();
  socks_.emplace(p, std::move(new_connection));
  int fd = pipes[0];
  int outfd = static_cast<int>(syscall(__NR_memfd_create, "data", 0));
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  base::WeakPtr<PipeSender> weak_this = weak_factory_.GetWeakPtr();
  task_runner_->AddFileDescriptorWatch(fd, [fd, outfd, weak_this] {
    if (!weak_this) {
      close(fd);
      close(outfd);
      return;
    }

    long rd = PERFETTO_EINTR(
        splice(fd, nullptr, outfd, nullptr, 16 * 4096, SPLICE_F_NONBLOCK));
    PERFETTO_CHECK(rd != -1 || errno == EAGAIN || errno == EWOULDBLOCK);
    if (rd == -1)
      return;
    total_read += rd;
    if ((total_read / 10000000) != ((total_read - rd) / 10000000))
      PERFETTO_LOG("perfhd: %lu\n", total_read);
    if (rd == 0) {
      weak_this->task_runner_->RemoveFileDescriptorWatch(fd);
      close(outfd);
      close(fd);
    }
  });
}

void PipeSender::OnDisconnect(ipc::UnixSocket* self) {
  socks_.erase(self);
}

int ProfHDMain(int argc, char** argv);
int ProfHDMain(int argc, char** argv) {
  if (argc != 2)
    return 1;

  base::UnixTaskRunner task_runner;
  PipeSender listener(&task_runner);

  std::unique_ptr<ipc::UnixSocket> sock(
      ipc::UnixSocket::Listen(argv[1], &listener, &task_runner));
  task_runner.Run();
  return 0;
}

}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::ProfHDMain(argc, argv);
}
