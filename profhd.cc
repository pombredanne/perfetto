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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/base/weak_ptr.h"
#include "src/ipc/unix_socket.h"

namespace perfetto {

class PipeSender : public ipc::UnixSocket::EventListener {
 public:
  PipeSender(base::TaskRunner* task_runner)
      : task_runner_(task_runner), weak_factory_(this) {}

  void OnNewIncomingConnection(ipc::UnixSocket*,
                               std::unique_ptr<ipc::UnixSocket>) override;

 private:
  base::TaskRunner* task_runner_;
  base::WeakPtrFactory<PipeSender> weak_factory_;
};

void PipeSender::OnNewIncomingConnection(
    ipc::UnixSocket*,
    std::unique_ptr<ipc::UnixSocket> new_connection) {
  PERFETTO_LOG("STUFF!");
  int pipes[2];
  PERFETTO_CHECK(pipe(pipes) != -1);
  new_connection->Send("data", 4, pipes[1]);
  int fd = pipes[0];
  base::WeakPtr<PipeSender> weak_this = weak_factory_.GetWeakPtr();
  task_runner_->AddFileDescriptorWatch(fd, [fd, weak_this] {
    if (!weak_this)
      return;

    char foo[4096];
    long rd = PERFETTO_EINTR(read(fd, &foo, sizeof(foo)));
    PERFETTO_CHECK(rd != -1);
    printf("%lu\n", rd);
    if (rd == 0)
      weak_this->task_runner_->RemoveFileDescriptorWatch(fd);
  });
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
