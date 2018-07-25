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

#include <ios>
#include <iostream>

#include <inttypes.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/base/unix_task_runner.h"
#include "src/ipc/unix_socket.h"

namespace perfetto {
namespace {

class MemReceiver : public ipc::UnixSocket::EventListener {
 public:
  MemReceiver(base::ScopedFile* mem_fd) : mem_fd_(mem_fd) {}

  void OnNewIncomingConnection(ipc::UnixSocket*,
                               std::unique_ptr<ipc::UnixSocket>) override;
  void OnDisconnect(ipc::UnixSocket* self) override;
  void OnDataAvailable(ipc::UnixSocket* sock) override {
    char buf[1];
    sock->Receive(&buf, sizeof(buf), mem_fd_);
  }

 private:
  std::map<ipc::UnixSocket*, std::unique_ptr<ipc::UnixSocket>> socks_;
  base::ScopedFile* mem_fd_;
};

void MemReceiver::OnNewIncomingConnection(
    ipc::UnixSocket*,
    std::unique_ptr<ipc::UnixSocket> new_connection) {
  ipc::UnixSocket* p = new_connection.get();
  socks_.emplace(p, std::move(new_connection));
}

void MemReceiver::OnDisconnect(ipc::UnixSocket* self) {
  socks_.erase(self);
}

int ReadMemServMain(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "argc");
    abort();
  }

  base::ScopedFile mem_fd;
  MemReceiver mem_recv(&mem_fd);
  base::UnixTaskRunner task_runner;

  std::unique_ptr<ipc::UnixSocket> sock(
      ipc::UnixSocket::Listen(argv[1], &mem_recv, &task_runner));
  std::thread th([&task_runner, &mem_fd] {
    for (;;) {
      int64_t addr;
      std::cin >> std::hex >> addr;
      task_runner.PostTask([&mem_fd, addr] {
        lseek(*mem_fd, addr, SEEK_SET);
        char buf[1];
        ssize_t rd;
        if ((rd = read(*mem_fd, buf, sizeof(buf))) == -1) {
          fprintf(stderr, "memfd read %d %" PRIi64 "\n", *mem_fd, addr);
          perror("memfd read");
          abort();
        }
        printf("read %zd bytes\n", rd);
      });
    }
  });
  task_runner.Run();
  return 0;
}
}  // namespace
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::ReadMemServMain(argc, argv);
}
