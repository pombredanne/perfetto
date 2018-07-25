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

#include <sys/socket.h>
#include <sys/types.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/base/unix_task_runner.h"
#include "src/ipc/unix_socket.h"

namespace perfetto {
namespace {

class MemSender : public ipc::UnixSocket::EventListener {
 public:
  MemSender(base::ScopedFile* mem_fd) : mem_fd_(mem_fd) {}
  void OnConnect(ipc::UnixSocket* self, bool connected) override;

 private:
  base::ScopedFile* mem_fd_;
};

void MemSender::OnConnect(ipc::UnixSocket* self, bool connected) {
  if (!connected) {
    fprintf(stderr, "not connected\n");
    return;
  }
  self->Send("x", 1, **mem_fd_);
}

int ReadMemClientMain(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "argc\n");
    abort();
  }
  base::ScopedFile fd(open("/proc/self/mem", O_RDONLY));
  if (*fd == -1) {
    perror("open");
    abort();
  }

  base::UnixTaskRunner task_runner;
  MemSender sender(&fd);

  std::unique_ptr<ipc::UnixSocket> sock =
      ipc::UnixSocket::Connect(argv[1], &sender, &task_runner);

  task_runner.Run();

  return 0;
}

}  // namespace
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::ReadMemClientMain(argc, argv);
}
