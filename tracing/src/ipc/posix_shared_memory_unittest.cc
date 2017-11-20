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

#include "tracing/src/ipc/posix_shared_memory.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/scoped_file.h"
#include "base/test/test_task_runner.h"
#include "base/utils.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace {

bool FileDescriptorIsClosed(int fd) {
  return lseek(fd, 0, SEEK_CUR) == -1 && errno == EBADF;
}

TEST(PosixSharedMemoryTest, DestructorUnmapsMemory) {
  PosixSharedMemory::Factory factory;
  std::unique_ptr<SharedMemory> shm = factory.CreateSharedMemory(4096);
  void* shm_start = shm->start();
  size_t shm_size = shm->size();
  ASSERT_NE(nullptr, shm_start);
  ASSERT_EQ(4096u, shm_size);

  memcpy(shm_start, "test", 5);
  char is_mapped[4] = {};
  ASSERT_EQ(0, mincore(shm_start, shm_size, is_mapped));
  ASSERT_NE(0, is_mapped[0]);

  shm.reset();
  ASSERT_EQ(0, mincore(shm_start, shm_size, is_mapped));
  ASSERT_EQ(0, is_mapped[0]);
}

TEST(PosixSharedMemoryTest, DestructorClosesFD) {
  std::unique_ptr<PosixSharedMemory> shm = PosixSharedMemory::Create(4096);
  int fd = shm->fd();
  ASSERT_GE(fd, 0);
  ASSERT_EQ(4096u, lseek(fd, 0, SEEK_END));

  shm.reset();
  ASSERT_TRUE(FileDescriptorIsClosed(fd));
}

TEST(PosixSharedMemoryTest, AttachToFd) {
  static const char kTmpPath[] = "/tmp/perfetto-shm-test";
  base::ScopedFile fd(open(kTmpPath, O_CREAT | O_RDWR | O_TRUNC));
  unlink(kTmpPath);
  const int fd_num = *fd;
  ASSERT_TRUE(fd);
  ASSERT_EQ(0, ftruncate(*fd, 4096));
  ASSERT_EQ(7, PERFETTO_EINTR(write(*fd, "foobar", 7)));

  std::unique_ptr<PosixSharedMemory> shm =
      PosixSharedMemory::AttachToFd(std::move(fd));
  ASSERT_NE(nullptr, shm->start());
  ASSERT_EQ(4096u, shm->size());
  ASSERT_EQ(0, memcmp("foobar", shm->start(), 7));

  ASSERT_FALSE(FileDescriptorIsClosed(fd_num));
  shm.reset();
  ASSERT_TRUE(FileDescriptorIsClosed(fd_num));
}

}  // namespace
}  // namespace perfetto
