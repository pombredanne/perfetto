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

#ifndef SRC_PROFILING_MEMORY_CLIENT_H_
#define SRC_PROFILING_MEMORY_CLIENT_H_

#include <stddef.h>

#include <mutex>
#include <vector>

#include "perfetto/base/scoped_file.h"

namespace perfetto {

class SocketPool;

// Cache for frees that have been observed. It is infeasible to send every
// free separately, so we batch and send the whole buffer once it is full.
class FreePage {
 public:
  FreePage();

  // Add address to buffer. Flush if necessary using a socket borrowed from
  // pool.
  // Can be called from any thread. Must not hold mtx_.`
  void Add(const uint64_t addr, uint64_t sequence_number, SocketPool* pool);

 private:
  // Needs to be called holding mtx_.
  void Flush(SocketPool* pool);

  std::vector<uint64_t> free_page_;
  std::mutex mtx_;
  size_t offset_;
};

// Socket borrowed from a SocketPool. Gets returned once it goes out of scope.
class BorrowedSocket {
 public:
  BorrowedSocket(const BorrowedSocket&) = delete;
  BorrowedSocket& operator=(const BorrowedSocket&) = delete;
  BorrowedSocket(BorrowedSocket&& other) noexcept {
    fd_ = std::move(other.fd_);
    socket_pool_ = other.socket_pool_;
    other.socket_pool_ = nullptr;
  }

  BorrowedSocket(base::ScopedFile fd, SocketPool* socket_pool)
      : fd_(std::move(fd)), socket_pool_(socket_pool) {}
  int operator*();
  int get();
  void Close();
  ~BorrowedSocket();

 private:
  base::ScopedFile fd_;
  SocketPool* socket_pool_ = nullptr;
};

class SocketPool {
 public:
  friend class BorrowedSocket;
  SocketPool(std::vector<base::ScopedFile> sockets);

  BorrowedSocket Borrow();

 private:
  void Return(base::ScopedFile fd);
  std::mutex mtx_;
  std::condition_variable cv_;
  std::vector<base::ScopedFile> sockets_;
  size_t available_sockets_;
  size_t dead_sockets_ = 0;
};

char* GetMainThreadStackBase();
char* GetThreadStackBase();

class Client {
 public:
  Client(std::vector<base::ScopedFile> sockets);
  Client(const std::string& sock_name, size_t conns);
  void RecordMalloc(uint64_t alloc_size, uint64_t alloc_address);
  void RecordFree(uint64_t alloc_address);

 private:
  char* GetStackBase();

  SocketPool socket_pool_;
  FreePage free_page_;
  char* const main_thread_stack_base_;
  std::atomic<uint64_t> sequence_number_{0};
};

}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_CLIENT_H_
