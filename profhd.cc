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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/base/weak_ptr.h"
#include "src/ipc/unix_socket.h"

#include <unwindstack/Elf.h>
#include <unwindstack/Memory.h>
#include <unwindstack/Regs.h>
#include <unwindstack/RegsArm.h>
#include <unwindstack/RegsArm64.h>
#include <unwindstack/RegsMips.h>
#include <unwindstack/RegsMips64.h>
#include <unwindstack/RegsX86.h>
#include <unwindstack/RegsX86_64.h>
#include <unwindstack/Unwinder.h>
#include <unwindstack/UserArm.h>
#include <unwindstack/UserArm64.h>
#include <unwindstack/UserMips.h>
#include <unwindstack/UserMips64.h>
#include <unwindstack/UserX86.h>
#include <unwindstack/UserX86_64.h>

namespace perfetto {

class StackMemory : public unwindstack::MemoryRemote {
 public:
  StackMemory(pid_t pid, uint64_t sp, uint8_t* stack, size_t size)
      : MemoryRemote(pid), sp_(sp), stack_(stack), size_(size) {}

  size_t Read(uint64_t addr, void* dst, size_t size) override {
    int64_t offset = addr - sp_;
    if (offset >= 0 && offset < size_) {
      PERFETTO_CHECK(size < size_ - offset);
      memcpy(dst, stack_ + offset, size);
      return size;
    }
    return unwindstack::MemoryRemote::Read(addr, dst, size);
  }

 private:
  uint64_t sp_;
  uint8_t* stack_;
  size_t size_;
};

constexpr uint8_t kAlloc = 1;
constexpr uint8_t kFree = 2;

struct MetadataHeader {
  uint8_t type;
};

struct AllocMetadata {
  MetadataHeader header;
  unwindstack::ArchEnum arch;
  uint8_t regs[264];
  uint64_t pid;
  uint64_t size;
  uint64_t sp;
  uint64_t addr;
};

unwindstack::Regs* CreateFromRawData(unwindstack::ArchEnum arch,
                                     void* raw_data) {
  switch (arch) {
    case unwindstack::ARCH_X86:
      return unwindstack::RegsX86::Read(raw_data);
    case unwindstack::ARCH_X86_64:
      return unwindstack::RegsX86_64::Read(raw_data);
    case unwindstack::ARCH_ARM:
      return unwindstack::RegsArm::Read(raw_data);
    case unwindstack::ARCH_ARM64:
      return unwindstack::RegsArm64::Read(raw_data);
    case unwindstack::ARCH_MIPS:
      return unwindstack::RegsMips::Read(raw_data);
    case unwindstack::ARCH_MIPS64:
      return unwindstack::RegsMips64::Read(raw_data);
    case unwindstack::ARCH_UNKNOWN:
    default:
      return nullptr;
  }
}

class HeapDump {
 public:
  void AddStack(const std::vector<unwindstack::FrameData>& data,
                const AllocMetadata& metadata) {
    if (data.size() <= 2) {
      return;
    }
    std::set<std::string> fns;
    for (const unwindstack::FrameData& frame_data : data) {
      fns.emplace(frame_data.function_name);
    }

    for (std::string function : fns) {
      auto itr = heap_usage_per_function_.find(function);
      if (itr != heap_usage_per_function_.end()) {
        itr->second += metadata.size;
      } else {
        heap_usage_per_function_.emplace(std::move(function), metadata.size);
      }
    }
  }

  void Print() {
    for (const auto& p : heap_usage_per_function_)
      PERFETTO_LOG("Heap Dump: %s %" PRIu64, p.first.c_str(), p.second);
  }

 private:
  std::map<std::string, uint64_t> heap_usage_per_function_;
};

std::map<uint64_t, HeapDump> heapdump_for_pid;

void DoneAlloc(void* mem, size_t sz) {
  AllocMetadata* metadata = reinterpret_cast<AllocMetadata*>(mem);
  unwindstack::Regs* regs = CreateFromRawData(metadata->arch, metadata->regs);
  if (regs == nullptr) {
    PERFETTO_ELOG("regs");
    return;
  }
  uint8_t* stack = reinterpret_cast<uint8_t*>(mem) + sizeof(AllocMetadata);
  unwindstack::RemoteMaps maps(metadata->pid);
  if (!maps.Parse()) {
    PERFETTO_LOG("Parse %" PRIu64, metadata->pid);
    return;
  }
  std::shared_ptr<unwindstack::Memory> mems = std::make_shared<StackMemory>(
      metadata->pid, reinterpret_cast<uint64_t>(metadata->sp), stack,
      sz - sizeof(AllocMetadata));
  unwindstack::Unwinder unwinder(1000, &maps, regs, mems);
  unwinder.Unwind();

  heapdump_for_pid[metadata->pid].AddStack(unwinder.frames(), *metadata);
}

void DoneFree(void* mem, size_t sz) {}

void Done(base::ScopedFile fd, size_t sz);
void Done(base::ScopedFile fd, size_t sz) {
  void* mem = mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, *fd, 0);
  if (mem == MAP_FAILED) {
    PERFETTO_PLOG("mmap %zd %d", sz, *fd);
    return;
  }
  MetadataHeader* header = reinterpret_cast<MetadataHeader*>(mem);
  switch (header->type) {
    case kAlloc:
      DoneAlloc(mem, sz);
      break;
    case kFree:
      DoneFree(mem, sz);
      break;
    default:
      PERFETTO_CHECK(false);
  }

  //  PERFETTO_LOG("Heap Dump for %" PRIu64, metadata->pid);
  //  heapdump_for_pid[metadata->pid].Print();
  munmap(mem, sz);
}

class RecordReader {
 public:
  RecordReader() { Reset(); }

  ssize_t Read(int fd) {
    if (read_idx_ < sizeof(record_size_)) {
      ssize_t rd = ReadRecordSize(fd);
      if (rd != -1) read_idx_ += rd;
      return rd;
    }

    ssize_t rd = ReadRecord(fd);
    if (rd != -1) read_idx_ += rd;
    if (done()) {
      Done(std::move(outfd_), record_size_);
      Reset();
    }
    return rd;
  }

 private:
  void Reset() {
    outfd_.reset(static_cast<int>(syscall(__NR_memfd_create, "data", 0)));
    read_idx_ = 0;
    record_size_ = 1337;
  }

  bool done() {
    return read_idx_ >= sizeof(record_size_) &&
           read_idx_ - sizeof(record_size_) == record_size_;
  }

  size_t read_idx() {
    if (read_idx_ < sizeof(record_size_)) return read_idx_;
    return read_idx_ - sizeof(record_size_);
  }

  ssize_t ReadRecordSize(int fd) {
    ssize_t rd = PERFETTO_EINTR(
        read(fd, reinterpret_cast<uint8_t*>(&record_size_) + read_idx_,
             sizeof(record_size_) - read_idx_));
    PERFETTO_CHECK(rd != -1 || errno == EAGAIN || errno == EWOULDBLOCK);
    return rd;
  }

  ssize_t ReadRecord(int fd) {
    static uint64_t chunk_size = 16u * 4096u;
    ssize_t rd = PERFETTO_EINTR(splice(
        fd, nullptr, *outfd_, nullptr,
        std::min(chunk_size, record_size_ - read_idx()), SPLICE_F_NONBLOCK));
    PERFETTO_CHECK(rd != -1 || errno == EAGAIN || errno == EWOULDBLOCK);
    return rd;
  }

  base::ScopedFile outfd_;
  uint64_t read_idx_;
  uint64_t record_size_;
};

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
    ipc::UnixSocket*, std::unique_ptr<ipc::UnixSocket> new_connection) {
  int pipes[2];
  PERFETTO_CHECK(pipe(pipes) != -1);
  new_connection->Send("x", 1, pipes[1]);
  ipc::UnixSocket* p = new_connection.get();
  socks_.emplace(p, std::move(new_connection));
  int fd = pipes[0];
  // We do not want to block the event loop in case of
  // spurious wake-ups.
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  base::WeakPtr<PipeSender> weak_this = weak_factory_.GetWeakPtr();
  // Cannot move into lambda in C++11.
  RecordReader* record_reader = new RecordReader();
  task_runner_->AddFileDescriptorWatch(fd, [fd, weak_this, record_reader] {
    if (!weak_this) {
      PERFETTO_LOG("Pipe closed");
      close(fd);
      delete record_reader;
      return;
    }

    ssize_t rd = record_reader->Read(fd);
    if (rd == -1) return;
    if (rd == 0) {
      PERFETTO_LOG("Pipe closed");
      weak_this->task_runner_->RemoveFileDescriptorWatch(fd);
      close(fd);
      delete record_reader;
      return;
    }
  });
}

void PipeSender::OnDisconnect(ipc::UnixSocket* self) { socks_.erase(self); }

int ProfHDMain(int argc, char** argv);
int ProfHDMain(int argc, char** argv) {
  if (argc != 2) return 1;

  base::UnixTaskRunner task_runner;
  PipeSender listener(&task_runner);

  std::unique_ptr<ipc::UnixSocket> sock(
      ipc::UnixSocket::Listen(argv[1], &listener, &task_runner));
  task_runner.Run();
  return 0;
}

}  // namespace perfetto

int main(int argc, char** argv) { return perfetto::ProfHDMain(argc, argv); }
