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
#include <thread>

#include <unistd.h>
#include "folly/ProducerConsumerQueue.h"
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

std::string GetName(int fd) {
  std::string path = "/proc/self/fd/";
  path += std::to_string(fd);
  char buf[512];
  readlink(path.c_str(), buf, sizeof(buf));
  return std::string(buf, sizeof(buf));
}

class StackMemory : public unwindstack::MemoryRemote {
 public:
  StackMemory(pid_t pid, uint64_t sp, uint8_t* stack, size_t size)
      : MemoryRemote(pid), sp_(sp), stack_(stack), size_(size) {}

  size_t Read(uint64_t addr, void* dst, size_t size) override {
    int64_t offset = addr - sp_;
    if (offset >= 0 && offset < size_) {
      if (size > size_ - offset)
        return false;
      memcpy(dst, stack_ + offset, std::min(size, size_));
      return std::min(size, size_);
    }
    return unwindstack::MemoryRemote::Read(addr, dst, size);
  }

  void SetStack(uint8_t* stack, size_t size) {
    stack_ = stack;
    size_ = size;
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
  uint64_t pid;
};

struct AllocMetadata {
  MetadataHeader header;
  unwindstack::ArchEnum arch;
  uint8_t regs[264];
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

struct Frame {
  Frame() {}
  Frame(unwindstack::FrameData fd) : data(std::move(fd)) {}

  unwindstack::FrameData data;
  size_t size = 0;
  std::map<std::string, Frame> children;

  void Print(std::ostream& o) const {
    o << "{";
    bool prev = false;
    if (!data.function_name.empty()) {
      if (prev)
        o << ",";
      prev = true;
      o << " \"name\": \"" << data.map_name << "`" << data.function_name
        << "\"\n";
    }
    if (prev)
      o << ",";
    prev = true;
    o << "  \"value\": " << size << "\n";
    if (!children.empty()) {
      if (prev)
        o << ",";
      prev = true;
      o << "  \"children\": [";
      bool first = true;
      for (const auto& c : children) {
        if (!first)
          o << ",\n";
        first = false;
        c.second.Print(o);
      }
      o << "]\n";
    }
    o << "}";
  }
};

class HeapDump {
 public:
  void AddStack(const std::vector<unwindstack::FrameData>& data,
                const AllocMetadata& metadata) {
    if (data.size() <= 2) {
      return;
    }
    std::lock_guard<std::mutex> l(mutex_);

    Frame* frame = &top_frame_;
    frame->size += metadata.size;
    for (auto it = data.rbegin(); it != data.rend(); ++it) {
      const unwindstack::FrameData& frame_data = *it;
      auto itr = frame->children.find(frame_data.function_name);
      if (itr == frame->children.end()) {
        auto pair =
            frame->children.emplace(frame_data.function_name, frame_data);
        PERFETTO_CHECK(pair.second);
        itr = pair.first;
      }
      itr->second.size += metadata.size;
      frame = &itr->second;
    }

    addr_info_.emplace(metadata.addr, std::make_pair(data, metadata));
  }

  void FreeAddr(uint64_t addr) {
    std::lock_guard<std::mutex> l(mutex_);
    auto itr = addr_info_.find(addr);
    if (itr == addr_info_.end())
      return;

    const std::vector<unwindstack::FrameData>& data = itr->second.first;
    const AllocMetadata& metadata = itr->second.second;

    Frame* frame = &top_frame_;
    frame->size -= metadata.size;
    for (const unwindstack::FrameData frame_data : data) {
      auto itr = frame->children.find(frame_data.function_name);
      if (itr == frame->children.end())
        break;
      itr->second.size -= metadata.size;
      frame = &itr->second;
    }

    addr_info_.erase(addr);
  }

  void Print(std::ostream& o) {
    std::lock_guard<std::mutex> l(mutex_);
    top_frame_.Print(o);
  }

 private:
  std::mutex mutex_;
  Frame top_frame_;
  std::map<uint64_t,
           std::pair<std::vector<unwindstack::FrameData>, AllocMetadata>>
      addr_info_;
};

std::map<uint64_t, HeapDump> heapdump_for_pid;

void DoneAlloc(void* mem, size_t sz) {
  if (sz < sizeof(AllocMetadata))
    return;
  AllocMetadata* metadata = reinterpret_cast<AllocMetadata*>(mem);
  unwindstack::Regs* regs = CreateFromRawData(metadata->arch, metadata->regs);
  if (regs == nullptr) {
    PERFETTO_ELOG("regs");
    return;
  }
  uint8_t* stack = reinterpret_cast<uint8_t*>(mem) + sizeof(AllocMetadata);
  unwindstack::RemoteMaps maps(metadata->header.pid);
  if (!maps.Parse()) {
    // PERFETTO_LOG("Parse %" PRIu64, metadata->header.pid);
    return;
  }
  std::shared_ptr<unwindstack::Memory> mems = std::make_shared<StackMemory>(
      metadata->header.pid, metadata->sp, stack, sz - sizeof(AllocMetadata));
  unwindstack::Unwinder unwinder(1000, &maps, regs, mems);
  unwinder.Unwind();

  /*
  if (unwinder.LastErrorCode() != 0)
    PERFETTO_ELOG("Unwinder: %" PRIu8, unwinder.LastErrorCode());
*/
  heapdump_for_pid[metadata->header.pid].AddStack(unwinder.frames(), *metadata);
}

void DoneFree(void* mem, size_t sz) {
  MetadataHeader* header = reinterpret_cast<MetadataHeader*>(mem);
  uint64_t* freed = reinterpret_cast<uint64_t*>(mem);
  for (size_t n = 3; n < sz / sizeof(*freed); n++) {
    heapdump_for_pid[header->pid].FreeAddr(freed[n]);
  }
}

std::atomic<uint64_t> samples_recv(0);
std::atomic<uint64_t> samples_handled(0);

void Done(base::ScopedFile fd, size_t sz);
void Done(base::ScopedFile fd, size_t sz) {
  samples_handled++;
  if (sz < sizeof(MetadataHeader))
    return;
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
      PERFETTO_ELOG("Invalid type %" PRIu8, header->type);
  }
  munmap(mem, sz);
}

struct WorkItem {
 public:
  WorkItem() {}
  WorkItem(base::ScopedFile f, size_t s) : fd(std::move(f)), record_size(s) {}
  base::ScopedFile fd;
  size_t record_size;
};

std::atomic<uint64_t> queue_overrun(0);

class WorkQueue {
 public:
  bool Submit(WorkItem item) {
    if (queue_.write(std::move(item))) {
      task_runner_.PostTask([this] {
        WorkItem w;
        if (queue_.read(w))
          Done(std::move(w.fd), w.record_size);
      });
      return true;
    }
    return false;
  }

  void Run() { task_runner_.Run(); }

 private:
  folly::ProducerConsumerQueue<WorkItem> queue_{5000};
  base::UnixTaskRunner task_runner_;
};

class RecordReader {
 public:
  RecordReader() { Reset(); }

  ssize_t Read(int fd, WorkQueue* wq) {
    if (read_idx_ < sizeof(record_size_)) {
      ssize_t rd = ReadRecordSize(fd);
      if (rd != -1)
        read_idx_ += rd;
      return rd;
    }

    ssize_t rd = ReadRecord(fd);
    if (rd != -1)
      read_idx_ += rd;
    if (done()) {
      samples_recv++;
      if (!outfd_ || !wq->Submit(WorkItem(std::move(outfd_), record_size_)))
        queue_overrun++;
      Reset();
    }
    return rd;
  }

 private:
  void Reset() {
    outfd_.reset(static_cast<int>(syscall(__NR_memfd_create, "data", 0)));
    if (*outfd_ == -1)
      PERFETTO_PLOG("memfd_create");
    read_idx_ = 0;
    record_size_ = 1337;
  }

  bool done() {
    return read_idx_ >= sizeof(record_size_) &&
           read_idx_ - sizeof(record_size_) == record_size_;
  }

  size_t read_idx() {
    if (read_idx_ < sizeof(record_size_))
      return read_idx_;
    return read_idx_ - sizeof(record_size_);
  }

  ssize_t ReadRecordSize(int fd) {
    ssize_t rd = PERFETTO_EINTR(
        read(fd, reinterpret_cast<uint8_t*>(&record_size_) + read_idx_,
             sizeof(record_size_) - read_idx_));
    if (rd == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
      PERFETTO_PLOG("read %d", fd);
    PERFETTO_CHECK(rd != -1 || errno == EAGAIN || errno == EWOULDBLOCK);
    return rd;
  }

  ssize_t ReadRecord(int fd) {
    static uint64_t chunk_size = 16u * 4096u;

    ssize_t rd;
    if (outfd_) {
      rd = PERFETTO_EINTR(splice(
          fd, nullptr, *outfd_, nullptr,
          std::min(chunk_size, record_size_ - read_idx()), SPLICE_F_NONBLOCK));
    } else {
      char buf[4096];
      // Consume the data to not block the pipe.
      rd = PERFETTO_EINTR(read(fd, buf, sizeof(buf)));
    }
    if (rd == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
      PERFETTO_PLOG("splice %d -> %d", fd, *outfd_);
    PERFETTO_CHECK(rd != -1 || errno == EAGAIN || errno == EWOULDBLOCK);
    return rd;
  }

  base::ScopedFile outfd_;
  uint64_t read_idx_;
  uint64_t record_size_;
};

class PipeSender : public ipc::UnixSocket::EventListener {
 public:
  PipeSender(base::TaskRunner* task_runner, std::vector<WorkQueue>* work_queues)
      : task_runner_(task_runner),
        work_queues_(work_queues),
        num_wq_(work_queues_->size()),
        weak_factory_(this) {}

  void OnNewIncomingConnection(ipc::UnixSocket*,
                               std::unique_ptr<ipc::UnixSocket>) override;
  void OnDisconnect(ipc::UnixSocket* self) override;
  void OnDataAvailable(ipc::UnixSocket* sock) override {
    char buf[4096];
    sock->Receive(&buf, sizeof(buf));
  }

 private:
  base::TaskRunner* task_runner_;
  std::vector<WorkQueue>* work_queues_;
  uint64_t cur_wq_ = 0;
  size_t num_wq_;
  std::map<ipc::UnixSocket*, std::unique_ptr<ipc::UnixSocket>> socks_;
  base::WeakPtrFactory<PipeSender> weak_factory_;
};

void PipeSender::OnNewIncomingConnection(
    ipc::UnixSocket*,
    std::unique_ptr<ipc::UnixSocket> new_connection) {
  int pipes[2];
  if (pipe(pipes) == -1) {
    PERFETTO_PLOG("pipe");
    new_connection->Shutdown(false);
    return;
  }
  new_connection->Send("x", 1, pipes[1]);
  close(pipes[1]);
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
      // PERFETTO_LOG("Pipe closed");
      close(fd);
      delete record_reader;
      return;
    }

    ssize_t rd = record_reader->Read(
        fd, &((*weak_this->work_queues_)[weak_this->cur_wq_++ %
                                         weak_this->num_wq_]));
    if (rd == 0) {
      // PERFETTO_LOG("Pipe closed %s", GetName(fd).c_str());
      weak_this->task_runner_->RemoveFileDescriptorWatch(fd);
      close(fd);
      delete record_reader;
      return;
    }
  });
}

void PipeSender::OnDisconnect(ipc::UnixSocket* self) {
  socks_.erase(self);
}

int dumppipes[2];

void DumpHeapsHandler(int sig) {
  write(dumppipes[1], "w", 1);
}

void DumpHeaps() {
  PERFETTO_LOG("Dumping heap dumps.");
  PERFETTO_LOG("Samples received: %" PRIu64 ", samples handled %" PRIu64
               ", samples overran %" PRIu64,
               samples_recv.load(), samples_handled.load(),
               queue_overrun.load());
  char buf[512];
  read(dumppipes[0], &buf, sizeof(buf));

  std::ofstream f("/data/local/heapd");
  f << "{\n";
  bool first = true;
  for (auto& p : heapdump_for_pid) {
    if (!first)
      f << ",\n";
    first = false;
    f << '"' << p.first << "\": [";
    p.second.Print(f);
    f << "]";
  }
  f << "\n}";
}

int ProfHDMain(int argc, char** argv);
int ProfHDMain(int argc, char** argv) {
  if (argc != 2)
    return 1;

  pipe(dumppipes);
  signal(SIGUSR1, DumpHeapsHandler);

  std::vector<WorkQueue> work_queues(
      std::max(1u, std::thread::hardware_concurrency()));

  base::UnixTaskRunner read_task_runner;
  base::UnixTaskRunner sighandler_task_runner;
  // Never block the read task_runner.
  sighandler_task_runner.AddFileDescriptorWatch(dumppipes[0], DumpHeaps);
  PipeSender listener(&read_task_runner, &work_queues);

  std::unique_ptr<ipc::UnixSocket> sock(
      ipc::UnixSocket::Listen(argv[1], &listener, &read_task_runner));

  std::vector<std::thread> threads;
  for (auto& wq : work_queues)
    threads.emplace_back([&wq] { wq.Run(); });
  threads.emplace_back(
      [&sighandler_task_runner] { sighandler_task_runner.Run(); });

  read_task_runner.Run();
  for (auto& t : threads)
    t.join();

  return 0;
}

}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::ProfHDMain(argc, argv);
}
