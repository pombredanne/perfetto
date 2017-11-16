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

#include "tracing/src/unix_rpc/unix_shared_memory.h"

#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include "tracing/src/core/base.h"

namespace perfetto {

// static
std::unique_ptr<UnixSharedMemory> UnixSharedMemory::Create(size_t size) {
  // TODO: use memfd_create on Linux/Android if the kernel supports is (needs
  // syscall.h, there is no glibc wrtapper). If not, on Android fallback on
  // ashmem and on Linux fallback on /dev/shm/perfetto-whatever.
  char path[64];
  sprintf(path, "/tmp/perfetto-shm-%d", getpid());

  // TODO use ScopedFd (have to introduce it in base.h). Right now this leaks
  // a fd if mmap fails.
  int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd < 0)
    return nullptr;
  unlink(path);
  if (ftruncate(fd, static_cast<off_t>(size)) < 0)
    return nullptr;
  return MapFD(fd, size);
}

// static
std::unique_ptr<UnixSharedMemory> UnixSharedMemory::AttachToFd(int fd) {
  struct stat stat_buf = {};
  if (fstat(fd, &stat_buf))
    return nullptr;
  DCHECK(stat_buf.st_size > 0);
  return MapFD(fd, static_cast<size_t>(stat_buf.st_size));
}

// static
std::unique_ptr<UnixSharedMemory> UnixSharedMemory::MapFD(int fd, size_t size) {
  DCHECK(fd >= 0);
  DCHECK(size > 0);
  void* start = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (start == MAP_FAILED)
    return nullptr;
  return std::unique_ptr<UnixSharedMemory>(
      new UnixSharedMemory(start, size, fd));
}

UnixSharedMemory::UnixSharedMemory(void* start, size_t size, int fd)
    : start_(start), size_(size), fd_(fd) {}

UnixSharedMemory::~UnixSharedMemory() {
  munmap(start(), size());
  close(fd_);
}

UnixSharedMemory::Factory::~Factory() {}

std::unique_ptr<SharedMemory> UnixSharedMemory::Factory::CreateSharedMemory(
    size_t size) {
  return UnixSharedMemory::Create(size);
}

}  // namespace perfetto
