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

#include <stdio.h>
#include <sys/mman.h>

#include "gtest/gtest.h"
#include "tracing/src/core/base.h"
#include "tracing/src/core/build_config.h"

namespace perfetto {
namespace {

// Mac OS X doesn't support abstract (i.e. unnamed) sockets.
#if BUILDFLAG(OS_MACOSX)
static const char kSocketName[] = "/tmp/test_socket";
void UnlinkSocket() {
  unlink(kSocketName);
}
#else
static const char kSocketName[] = "@test_socket";
void UnlinkSocket() {}
#endif

class UnixSocketTest : public ::testing::Test {
 protected:
  void SetUp() override { UnlinkSocket(); }
  void TearDown() override { UnlinkSocket(); }
};

TEST_F(UnixSocketTest, ClientServer) {
  UnixSocket srv;
  UnixSocket cli;
  ASSERT_TRUE(srv.Listen(kSocketName));
  ASSERT_TRUE(srv.is_listening());
  ASSERT_FALSE(srv.is_connected());
  ASSERT_TRUE(cli.Connect(kSocketName));
  UnixSocket srv_conn;
  ASSERT_TRUE(srv.Accept(&srv_conn));
  ASSERT_TRUE(cli.is_connected());
  ASSERT_TRUE(cli.Send("cli>srv"));
  ASSERT_TRUE(srv_conn.Send("srv>cli"));
  ASSERT_EQ("cli>srv", srv_conn.RecvString());
  ASSERT_EQ("srv>cli", cli.RecvString());

  // Check that Recv*() fails gracefully on a closed socket.
  cli.Shutdown();
  char msg[4];
  ASSERT_EQ(-1, cli.Recv(&msg, sizeof(msg)));
  ASSERT_EQ("", cli.RecvString());
  ASSERT_EQ(0, srv_conn.Recv(&msg, sizeof(msg)));
  ASSERT_EQ("", srv_conn.RecvString());
  srv.Shutdown();
}

TEST_F(UnixSocketTest, MoveOperators) {
  UnixSocket srv;
  UnixSocket cli_1;
  ASSERT_TRUE(srv.Listen(kSocketName));
  ASSERT_TRUE(cli_1.Connect(kSocketName));
  UnixSocket srv_conn_1;
  ASSERT_TRUE(srv.Accept(&srv_conn_1));

  // Move |cli_1| -> |cli_2|. After this |cli_1| will be uninitialized.
  ASSERT_TRUE(cli_1.is_connected());
  UnixSocket cli_2(std::move(cli_1));
  ASSERT_FALSE(cli_1.is_connected());
  ASSERT_TRUE(cli_2.is_connected());

  // Verify that |cli_2| is functional.
  ASSERT_TRUE(cli_2.Send("cli1>srv"));
  ASSERT_EQ("cli1>srv", srv_conn_1.RecvString());
  cli_1 = std::move(cli_2);
  ASSERT_TRUE(cli_1.is_connected());
  ASSERT_FALSE(cli_2.is_connected());

  // Now reuse |cli_2| to establish a new connection and check it is functonal.
  ASSERT_TRUE(cli_2.Connect(kSocketName));
  UnixSocket srv_conn_2;
  ASSERT_TRUE(srv.Accept(&srv_conn_2));
  ASSERT_TRUE(cli_2.Send("cli2>srv"));
  ASSERT_EQ("cli2>srv", srv_conn_2.RecvString());

  cli_1.Shutdown();
  cli_2.Shutdown();
  srv.Shutdown();
}

TEST_F(UnixSocketTest, MultipleClients) {
  UnixSocket srv;
  constexpr size_t kNumClients = 32;
  UnixSocket cli[kNumClients];
  UnixSocket srv_conn[kNumClients];
  ASSERT_TRUE(srv.Listen(kSocketName));

  for (int loop = 0; loop < 3; loop++) {
    for (size_t i = 0; i < kNumClients; i++)
      ASSERT_TRUE(cli[i].Connect(kSocketName));

    for (size_t i = 0; i < kNumClients; i++)
      ASSERT_TRUE(srv.Accept(&srv_conn[i]));

    for (size_t i = 0; i < kNumClients; i++) {
      char msg_cli_srv[32];
      sprintf(msg_cli_srv, "[%2zu]cli>srv", i);
      ASSERT_TRUE(cli[i].Send(msg_cli_srv));

      char msg_srv_cli[32];
      sprintf(msg_srv_cli, "[%2zu]src>cli", i);
      ASSERT_TRUE(srv_conn[i].Send(msg_srv_cli));

      ASSERT_EQ(msg_srv_cli, cli[i].RecvString());
      ASSERT_EQ(msg_cli_srv, srv_conn[i].RecvString());

      srv_conn[i].Shutdown();
      cli[i].Shutdown();
    }
  }
}

// Creates two processes. The server process creates a file and passes it over
// the socket to the client. Both processes mmap the file and check that
// see the same contents.
TEST_F(UnixSocketTest, SharedMemory) {
  pid_t pid = fork();
  ASSERT_GE(pid, 0);
  constexpr size_t kTmpSize = 4096;

  if (pid == 0) {
    UnixSocket srv;
    ASSERT_TRUE(srv.Listen(kSocketName));
    UnixSocket srv_conn;
    ASSERT_TRUE(srv.Accept(&srv_conn));
    FILE* tmp = tmpfile();
    ASSERT_NE(nullptr, tmp);
    int tmp_fd = fileno(tmp);
    ASSERT_FALSE(ftruncate(tmp_fd, kTmpSize));

    char* mem = reinterpret_cast<char*>(
        mmap(nullptr, kTmpSize, PROT_READ | PROT_WRITE, MAP_SHARED, tmp_fd, 0));
    ASSERT_NE(nullptr, mem);
    memcpy(mem, "shm rocks", 10);
    ASSERT_TRUE(srv_conn.Send("txfd", 5, &tmp_fd, 1));

    // Wait for the client to change this again.
    char msg[32];
    ASSERT_GT(srv_conn.Recv(msg, sizeof(msg)), 0);
    ASSERT_STREQ("change notify", msg);
    ASSERT_STREQ("rock more", mem);

    _exit(0);
  }

  UnixSocket cli;
  for (int attempt = 0; attempt < 100 && !cli.is_connected(); attempt++) {
    cli.Connect(kSocketName);
    usleep(1000);
  }
  ASSERT_TRUE(cli.is_connected());
  char msg[32];
  int tmp_fd[3];
  uint32_t tmp_fd_num = arraysize(tmp_fd);
  ASSERT_EQ(5, cli.Recv(msg, sizeof(msg), tmp_fd, &tmp_fd_num));
  ASSERT_STREQ("txfd", msg);

  // Check that we received exactly one file descriptor from the child process.
  ASSERT_EQ(1u, tmp_fd_num);
  char* mem = reinterpret_cast<char*>(mmap(
      nullptr, kTmpSize, PROT_READ | PROT_WRITE, MAP_SHARED, tmp_fd[0], 0));
  ASSERT_NE(nullptr, mem);
  mem[9] = '\0';  // Just to get a clean error in case of test failure.
  ASSERT_STREQ("shm rocks", mem);

  // Now change the shared memory and ping the other process.
  memcpy(mem, "rock more", 10);
  ASSERT_TRUE(cli.Send("change notify", 14));

  int st = 0;
  HANDLE_EINTR(waitpid(pid, &st, 0));
  ASSERT_FALSE(WIFSIGNALED(st)) << "Server died with signal " << WTERMSIG(st);
  EXPECT_TRUE(WIFEXITED(st));
  ASSERT_EQ(0, WEXITSTATUS(st));
}

// TODO(primiano): add a test to check that in the case of a peer sending a fd
// and the other end just doing a recv (without taking it), the fd is closed and
// not left around.

}  // namespace
}  // namespace perfetto
