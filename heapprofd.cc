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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>

#include <unistd.h>
#include "folly/ProducerConsumerQueue.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/base/unix_task_runner.h"
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

// #define MAYBE_LOCK(l, mtx) do { } while(0)
#define MAYBE_LOCK(l, mtx) std::lock_guard<std::mutex> l(mtx);

namespace perfetto {

std::string GetName(int fd) {
  std::string path = "/proc/self/fd/";
  path += std::to_string(fd);
  char buf[512];
  readlink(path.c_str(), buf, sizeof(buf));
  return std::string(buf, sizeof(buf));
}

namespace base {
using TimeMicros = std::chrono::microseconds;
inline TimeMicros GetWallTimeUs() {
  return std::chrono::duration_cast<TimeMicros>(GetWallTimeNs());
}
}  // namespace base

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

class Histogram {
 public:
  void AddSample(base::TimeMicros value) {
    MAYBE_LOCK(l, mtx_);
    samples_.emplace_back(value);
    return;
    total_time_ += value;
    total_samples_++;
    base::TimeMicros cur = base::TimeMicros(-1);
    for (auto& p : delay_histogram_ms_) {
      if (cur < value && value <= p.first) {
        p.second++;
        return;
      }
      cur = p.first;
    }
    PERFETTO_CHECK(false);
  }

  void PrintDebugInfo() {
    MAYBE_LOCK(l, mtx_);
    base::TimeMicros cur = base::TimeMicros(-1);
    for (auto& p : delay_histogram_ms_) {
      PERFETTO_LOG("(%" PRId64 ", %" PRId64 "]: %" PRIu64, int64_t(cur.count()),
                   int64_t(p.first.count()), p.second);
      cur = p.first;
    }
    PERFETTO_LOG("profhd: average: %" PRIu64,
                 uint64_t(total_time_.count()) / total_samples_);
  }

  void PrintJSON(std::ostream& f) {
    MAYBE_LOCK(l, mtx_);
    f << "[";
    auto last = samples_.cend();
    last--;
    for (auto itr = samples_.cbegin(); itr != samples_.cend(); ++itr) {
      f << itr->count();
      if (itr != last)
        f << ",";
    }
    f << "]";
  }

 private:
  std::mutex mtx_;

  base::TimeMicros total_time_{0};
  uint64_t total_samples_ = 0;
  std::vector<base::TimeMicros> samples_;
  std::vector<std::pair<base::TimeMicros, uint64_t>> delay_histogram_ms_{
      {{base::TimeMicros(1), 0},
       {base::TimeMicros(5), 0},
       {base::TimeMicros(10), 0},
       {base::TimeMicros(20), 0},
       {base::TimeMicros(50), 0},
       {base::TimeMicros(100), 0},
       {base::TimeMicros(200), 0},
       {base::TimeMicros(500), 0},
       {base::TimeMicros(1000), 0},
       {base::TimeMicros(5000), 0},
       {base::TimeMicros(10000), 0},
       {base::TimeMicros(50000), 0},
       {base::TimeMicros(100000), 0},
       {base::TimeMicros(500000), 0},
       {base::TimeMicros(1000000), 0},
       {base::TimeMicros(std::numeric_limits<base::TimeMicros::rep>::max()),
        0}}};
};

std::atomic<uint64_t> samples_recv(0);
std::atomic<uint64_t> samples_too_late(0);
std::atomic<uint64_t> samples_handled(0);
std::atomic<uint64_t> samples_failed(0);

std::atomic<uint64_t> frees_handled(0);
std::atomic<uint64_t> frees_found(0);

std::array<std::atomic<uint64_t>, 7> errors;
Histogram histogram;
Histogram unwind_only_histogram;
Histogram gap_histogram;
Histogram parse_only_histogram;
Histogram send_histogram;
Histogram alloc_histogram;
Histogram stack_histogram;
Histogram unwind_diff_histogram;
Histogram unwind_diff_factor_histogram;

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
  uint64_t sp_offset;
  uint64_t addr;
  uint64_t last_timing;
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

struct AddressMetadata {
  AddressMetadata(std::vector<unwindstack::FrameData> frs,
                  AllocMetadata am,
                  uint64_t n)
      : frames(frs), alloc_metadata(am), n_alloc(n) {}

  std::vector<unwindstack::FrameData> frames;
  AllocMetadata alloc_metadata;
  uint64_t n_alloc;
};

class HeapDump {
 public:
  void AddStack(const std::vector<unwindstack::FrameData>& data,
                const AllocMetadata& metadata,
                uint64_t n) {
    if (data.size() <= 2) {
      return;
    }
    MAYBE_LOCK(l, mutex_);

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

    addr_info_.emplace(std::piecewise_construct,
                       std::forward_as_tuple(metadata.addr),
                       std::forward_as_tuple(data, metadata, n));
  }

  uint64_t FreeAddr(uint64_t addr) {
    MAYBE_LOCK(l, mutex_);
    auto itr = addr_info_.find(addr);
    if (itr == addr_info_.end())
      return 0;

    const std::vector<unwindstack::FrameData>& data = itr->second.frames;
    const AllocMetadata& metadata = itr->second.alloc_metadata;
    const uint64_t n = itr->second.n_alloc;

    Frame* frame = &top_frame_;
    frame->size -= metadata.size;
    for (auto it = data.rbegin(); it != data.rend(); ++it) {
      const unwindstack::FrameData& frame_data = *it;
      auto itr = frame->children.find(frame_data.function_name);
      if (itr == frame->children.end())
        break;
      itr->second.size -= metadata.size;
      frame = &itr->second;
    }

    addr_info_.erase(itr);

    return n;
  }

  void Print(std::ostream& o) {
    MAYBE_LOCK(l, mutex_);
    top_frame_.Print(o);
  }

 private:
  std::mutex mutex_;
  Frame top_frame_;
  std::map<uint64_t, AddressMetadata> addr_info_;
};

struct Metadata {
  Metadata(uint64_t p) : maps(p), pid(p) { maps.Parse(); }
  HeapDump heap_dump;
  unwindstack::RemoteMaps maps;
  uint64_t pid;
  uint64_t num_allocs = 0;
  base::TimeMicros last_alloc{0};
  base::TimeMicros last_unwind_timing;
  std::atomic<int> pipes{0};
};

std::map<int, Metadata> metadata_for_pid;
std::mutex metadata_for_pid_mtx;
std::map<int, int> pipe_to_pid;

void DoneAlloc(void* mem, size_t sz, Metadata* metadata) {
  auto start = base::GetWallTimeUs();
  stack_histogram.AddSample(base::TimeMicros(sz));
  if (metadata->last_alloc != base::TimeMicros(0))
    gap_histogram.AddSample(start - metadata->last_alloc);

  metadata->last_alloc = start;
  metadata->num_allocs++;
  if (sz < sizeof(AllocMetadata)) {
    PERFETTO_ELOG("size");
    samples_failed++;
    return;
  }
  AllocMetadata* alloc_metadata = reinterpret_cast<AllocMetadata*>(mem);
  if (alloc_metadata->last_timing)
    send_histogram.AddSample(base::TimeMicros(alloc_metadata->last_timing));
  if (metadata->last_unwind_timing != base::TimeMicros(0) &&
      alloc_metadata->last_timing) {
    base::TimeMicros last_timing_us(alloc_metadata->last_timing);
    unwind_diff_histogram.AddSample(last_timing_us -
                                    metadata->last_unwind_timing);
  }

  unwindstack::Regs* regs =
      CreateFromRawData(alloc_metadata->arch, alloc_metadata->regs);
  if (regs == nullptr) {
    PERFETTO_ELOG("regs");
    samples_failed++;
    return;
  }
  uint8_t* stack = reinterpret_cast<uint8_t*>(mem) + alloc_metadata->sp_offset;
  std::shared_ptr<unwindstack::Memory> mems = std::make_shared<StackMemory>(
      alloc_metadata->header.pid, alloc_metadata->sp, stack,
      sz - alloc_metadata->sp_offset);
  unwindstack::Unwinder unwinder(1000, &metadata->maps, regs, mems);
  auto unwind_start = base::GetWallTimeUs();

  int error_code;
  for (int attempt = 0; attempt < 2; ++attempt) {
    unwinder.Unwind();
    error_code = unwinder.LastErrorCode();
    if (error_code != 0) {
      if (error_code == unwindstack::ERROR_INVALID_MAP && attempt == 0) {
        metadata->maps = unwindstack::RemoteMaps(metadata->pid);
        metadata->maps.Parse();
      } else {
        samples_failed++;
        if (error_code > 0 && error_code < errors.size())
          errors[error_code]++;
        else
          PERFETTO_ELOG("Unwinder: %" PRIu8, error_code);
        break;
      }
    } else {
      samples_handled++;
      break;
    }
  }
  if (error_code == 0) {
    metadata->heap_dump.AddStack(unwinder.frames(), *alloc_metadata,
                                 metadata->num_allocs);
  }
  base::TimeMicros now = base::GetWallTimeUs();
  base::TimeMicros unwind_time = now - unwind_start;
  histogram.AddSample(now - start);
  unwind_only_histogram.AddSample(unwind_time);
  metadata->last_unwind_timing = unwind_time;
}

void DoneFree(void* mem, size_t sz, Metadata* metadata) {
  uint64_t* freed = reinterpret_cast<uint64_t*>(mem);
  for (size_t n = 3; n < sz / sizeof(*freed); n++) {
    frees_handled++;
    if (uint64_t x = metadata->heap_dump.FreeAddr(freed[n])) {
      frees_found++;
      alloc_histogram.AddSample(base::TimeMicros(x));
    }
  }
}

void Done(std::unique_ptr<uint8_t[]> buf, size_t sz, int pipe_fd);
void Done(std::unique_ptr<uint8_t[]> buf, size_t sz, int pipe_fd) {
  if (sz < sizeof(MetadataHeader))
    return;
  void* mem = buf.get();
  MetadataHeader* header = reinterpret_cast<MetadataHeader*>(mem);
  decltype(metadata_for_pid)::iterator itr;
  {
    std::lock_guard<std::mutex> l(metadata_for_pid_mtx);
    itr = metadata_for_pid.find(header->pid);
    if (itr == metadata_for_pid.end()) {
      itr = metadata_for_pid.emplace(header->pid, header->pid).first;
    }

    auto itr2 = pipe_to_pid.find(pipe_fd);
    if (itr2 == pipe_to_pid.end()) {
      itr->second.pipes++;
      pipe_to_pid.emplace(pipe_fd, header->pid);
    }

  }
  Metadata& metadata = itr->second;

  switch (header->type) {
    case kAlloc:
      DoneAlloc(mem, sz, &metadata);
      break;
    case kFree:
      DoneFree(mem, sz, &metadata);
      break;
    default:
      PERFETTO_ELOG("Invalid type %" PRIu8, header->type);
  }
}

struct WorkItem {
 public:
  WorkItem() {}
  WorkItem(std::unique_ptr<uint8_t[]> f, size_t s, int pfd)
      : buf(std::move(f)), record_size(s), pipe_fd(pfd) {}
  std::unique_ptr<uint8_t[]> buf;
  size_t record_size;
  int pipe_fd;
};

std::atomic<uint64_t> samples_overran(0);

class WorkQueue {
 public:
  bool Submit(WorkItem item) {
    if (queue_.write(std::move(item))) {
      task_runner_.PostTask([this] {
        WorkItem w;
        if (queue_.read(w))
          Done(std::move(w.buf), w.record_size, w.pipe_fd);
      });
      return true;
    }
    return false;
  }

  void Run() { task_runner_.Run(); }

  base::UnixTaskRunner task_runner_;

 private:
  folly::ProducerConsumerQueue<WorkItem> queue_{5000};
};

class RecordReader {
 public:
  RecordReader() { Reset(); }

  ssize_t Read(int fd, std::vector<WorkQueue>* wqs) {
    if (read_idx_ < sizeof(record_size_)) {
      ssize_t rd = ReadRecordSize(fd);
      if (rd != -1)
        read_idx_ += rd;
      if (read_idx_ == sizeof(record_size_))
        buf_.reset(new uint8_t[record_size_]);
      return rd;
    }

    ssize_t rd = ReadRecord(fd);
    if (rd != -1)
      read_idx_ += rd;
    if (done()) {
      samples_recv++;
      MetadataHeader* header = reinterpret_cast<MetadataHeader*>(buf_.get());
      if (!(*wqs)[header->pid % wqs->size()].Submit(WorkItem(std::move(buf_), record_size_, fd)))
        samples_overran++;
      Reset();
    }
    return rd;
  }

 private:
  void Reset() {
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
    constexpr uint64_t chunk_size = 16u * 4096u;
    uint64_t sz = std::min(chunk_size, record_size_ - read_idx());
    ssize_t rd = read(fd, buf_.get() + read_idx(), sz);

    if (rd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
      PERFETTO_FATAL("read %d", fd);
    }
    return rd;
  }

  uint64_t read_idx_;
  uint64_t record_size_;
  std::unique_ptr<uint8_t[]> buf_;
};

class PipeSender : public ipc::UnixSocket::EventListener {
 public:
  PipeSender(std::vector<WorkQueue>* work_queues)
      : work_queues_(work_queues), num_wq_(work_queues_->size()) {}

  void OnNewIncomingConnection(ipc::UnixSocket*,
                               std::unique_ptr<ipc::UnixSocket>) override;
  void OnDisconnect(ipc::UnixSocket* self) override;
  void OnDataAvailable(ipc::UnixSocket* sock) override {
    int fd = sock->fd();
    ssize_t rd =
        record_readers_[sock].Read(fd, work_queues_);
    if (rd == 0) {
      char buf[1];
      sock->Receive(&buf, 1);
    }
  }

 private:
  std::vector<WorkQueue>* work_queues_;
  size_t num_wq_;
  std::map<ipc::UnixSocket*, std::unique_ptr<ipc::UnixSocket>> socks_;
  std::map<ipc::UnixSocket*, RecordReader> record_readers_;
};

void PipeSender::OnNewIncomingConnection(
    ipc::UnixSocket*,
    std::unique_ptr<ipc::UnixSocket> new_connection) {
  //  PERFETTO_LOG("New connection");
  ipc::UnixSocket* p = new_connection.get();
  socks_.emplace(p, std::move(new_connection));
}

void PipeSender::OnDisconnect(ipc::UnixSocket* self) {
  //  PERFETTO_LOG("Disconnected connection");
  int fd = self->fd();
  std::lock_guard<std::mutex> l(metadata_for_pid_mtx);
  auto itr = pipe_to_pid.find(fd);
  if (itr != pipe_to_pid.end()) {
    int pid = itr->second;
    (*work_queues_)[pid % num_wq_].task_runner_.PostTask([pid] {
      std::lock_guard<std::mutex> l(metadata_for_pid_mtx);
      auto itr2 = metadata_for_pid.find(pid);
      PERFETTO_CHECK(itr2 != metadata_for_pid.end());
      if (--itr2->second.pipes == 0)
        metadata_for_pid.erase(pid);
    });
  }
  record_readers_.erase(self);
  socks_.erase(self);
}

int infopipes[2];

void InfoHandler(int sig) {
  write(infopipes[1], "w", 1);
}

void Info() {
  std::ofstream f("/data/local/heapinfo");
  size_t pipe_metadata;
  {
    std::lock_guard<std::mutex> l(metadata_for_pid_mtx);
    pipe_metadata = metadata_for_pid.size();
  }

  f << "{\n"
    << "\"samples_recv\": " << samples_recv.load() << ",\n"
    << "\"samples_handled\": " << samples_handled.load() << ",\n"
    << "\"samples_overran\": " << samples_overran.load() << ",\n"
    << "\"samples_failed\": " << samples_failed.load() << ",\n"
    << "\"frees_handled\": " << frees_handled.load() << ",\n"
    << "\"frees_found\": " << frees_found.load() << ",\n"
    << "\"pipe_metadata\": " << pipe_metadata << ",\n";

  f << "\"total_time_histogram\": ";
  histogram.PrintJSON(f);
  f << ",\n";

  f << "\"unwind_only_histogram\": ";
  unwind_only_histogram.PrintJSON(f);
  f << ",\n";

  f << "\"alloc_histogram\": ";
  alloc_histogram.PrintJSON(f);
  f << ",\n";

  f << "\"unwind_diff_histogram\": ";
  unwind_diff_histogram.PrintJSON(f);
  f << ",\n";

  f << "\"unwind_diff_factor_histogram\": ";
  unwind_diff_factor_histogram.PrintJSON(f);
  f << ",\n";

  f << "\"gap_histogram\": ";
  gap_histogram.PrintJSON(f);
  f << ",\n";

  f << "\"stack_histogram\": ";
  stack_histogram.PrintJSON(f);
  f << ",\n";

  f << "\"send_histogram\": ";
  send_histogram.PrintJSON(f);
  f << "\n}\n";

  PERFETTO_LOG("Dumping heap dumps.");
  PERFETTO_LOG("Samples received: %" PRIu64 ", samples handled %" PRIu64
               ", samples overran %" PRIu64 ", samples failed %" PRIu64
               ", frees handled %" PRIu64 ", frees found %" PRIu64
               ", samples too late %" PRIu64 ", pipe metadata %" PRIu64,
               samples_recv.load(), samples_handled.load(),
               samples_overran.load(), samples_failed.load(),
               frees_handled.load(), frees_found.load(),
               samples_too_late.load(), pipe_metadata);
  for (int i = 1; i < errors.size(); ++i)
    PERFETTO_LOG("errors[%d] = %" PRIu64, i, errors[i].load());

  PERFETTO_LOG("Total time:");
  histogram.PrintDebugInfo();
  PERFETTO_LOG("Unwinding time:");
  unwind_only_histogram.PrintDebugInfo();
  PERFETTO_LOG("Alloc:");
  alloc_histogram.PrintDebugInfo();
  PERFETTO_LOG("Stack size:");
  stack_histogram.PrintDebugInfo();
  // PERFETTO_LOG("Parsing time:");
  // parse_only_histogram.PrintDebugInfo();
  PERFETTO_LOG("Sending time:");
  send_histogram.PrintDebugInfo();
  char buf[512];
  read(infopipes[0], &buf, sizeof(buf));
}

void Dump() {
  std::ofstream f("/data/local/heapd");
  f << "{\n";
  bool first = true;
  std::lock_guard<std::mutex> l(metadata_for_pid_mtx);
  for (auto& p : metadata_for_pid) {
    auto& m = p.second;
    if (!first)
      f << ",\n";
    first = false;
    f << '"' << m.pid << "\": [";
    m.heap_dump.Print(f);
    f << "]";
  }
  f << "\n}";
}

void DumpHandler(int sig) {
  std::thread t(Dump);
  t.detach();
}

int ProfHDMain(int argc, char** argv);
int ProfHDMain(int argc, char** argv) {
  unwindstack::Elf::SetCachingEnabled(true);
  if (argc != 2)
    return 1;

  for (int i = 0; i < errors.size(); ++i)
    errors[i] = 0;
  pipe(infopipes);
  signal(SIGUSR1, InfoHandler);
  signal(SIGUSR2, DumpHandler);

  std::vector<WorkQueue> work_queues(1);
//      std::max(1u, std::thread::hardware_concurrency()));

  base::UnixTaskRunner read_task_runner;
  base::UnixTaskRunner sighandler_task_runner;
  // Never block the read task_runner.
  sighandler_task_runner.AddFileDescriptorWatch(infopipes[0], Info);
  PipeSender listener(&work_queues);

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
