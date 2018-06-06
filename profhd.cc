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

#include <fstream>
#include <map>
#include <string>
#include <algorithm>

#include "perfetto/base/logging.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/base/weak_ptr.h"
#include "perfetto/base/scoped_file.h"
#include "src/ipc/unix_socket.h"

#include <unwindstack/Regs.h>
#include <unwindstack/RegsArm.h>
#include <unwindstack/RegsArm64.h>
#include <unwindstack/RegsMips.h>
#include <unwindstack/RegsMips64.h>
#include <unwindstack/RegsX86.h>
#include <unwindstack/RegsX86_64.h>
#include <unwindstack/UserArm.h>
#include <unwindstack/UserArm64.h>
#include <unwindstack/UserMips.h>
#include <unwindstack/UserMips64.h>
#include <unwindstack/UserX86.h>
#include <unwindstack/UserX86_64.h>
#include <unwindstack/Elf.h>
#include <unwindstack/Memory.h>
#include <unwindstack/Unwinder.h>


namespace perfetto {

class StackMemory : public unwindstack::MemoryRemote {
	public:
  StackMemory(pid_t pid, uint64_t sp, uint8_t* stack, size_t size) : MemoryRemote(pid), sp_(sp), size_(size) {}

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

static long total_read = 0;

struct Metadata {
  unwindstack::ArchEnum arch;
  uint8_t regs[66];
  int64_t pid;
  uint64_t size;
	void* sp;
};

unwindstack::Regs* CreateFromRawData(unwindstack::ArchEnum arch, void* raw_data) {
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

static int total_frames = 0;
static unsigned long total_records = 0;

void Done(int fd, size_t sz);
void Done(int fd, size_t sz) {
	PERFETTO_LOG("perfhd: records: %lu\n", total_records++);
	return;
  void* mem = mmap(nullptr, sz, PROT_READ, 0, fd, 0);
  Metadata* metadata = reinterpret_cast<Metadata*>(mem);
  uint8_t* stack = reinterpret_cast<uint8_t*>(mem);
  stack += sizeof(Metadata);
  unwindstack::Regs* regs = CreateFromRawData(metadata->arch, metadata->regs);
	unwindstack::RemoteMaps maps(metadata->pid);
	std::shared_ptr<unwindstack::Memory> mems = std::make_shared<StackMemory>(metadata->pid, reinterpret_cast<uint64_t>(metadata->sp), stack, metadata->size);
	unwindstack::Unwinder unwinder(1000, &maps, regs, mems);
	unwinder.Unwind();
	total_frames += unwinder.NumFrames();
	PERFETTO_LOG("Total frames: %d", total_frames);
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
		} else {
		 ssize_t rd = ReadRecord(fd);
		 if (rd != -1) read_idx_ += rd;
		 if (read_idx_ == record_size_) {
			 Done(*outfd_, record_size_);
			 Reset();
		 }
		 return rd;
		}
	}

	void Reset() {
		outfd_.reset(static_cast<int>(syscall(__NR_memfd_create, "data", 0)));
		read_idx_ = 0;
	}


	bool done() {
		return read_idx_ > sizeof(record_size_) && read_idx_ == record_size_;
	}

	int outfd() {
		return *outfd_;
	}

 private:
	ssize_t ReadRecordSize(int fd) {
		ssize_t rd = PERFETTO_EINTR(read(fd, reinterpret_cast<uint8_t*>(&record_size_) + read_idx_, sizeof(record_size_) - read_idx_));
		PERFETTO_CHECK(rd != -1 || errno == EAGAIN || errno == EWOULDBLOCK);
		PERFETTO_LOG("Record size %d %" PRIu64, rd, record_size_);
		return rd;
	}

	ssize_t ReadRecord(int fd) {
		static uint64_t chunk_size = 16u * 4096u;
		ssize_t rd = PERFETTO_EINTR(
			splice(fd, nullptr, *outfd_, nullptr, std::min(chunk_size, record_size_ - read_idx_), SPLICE_F_NONBLOCK));
		PERFETTO_CHECK(rd != -1 || errno == EAGAIN || errno == EWOULDBLOCK);
		return rd;
	}

	base::ScopedFile outfd_;
	uint8_t read_idx_;
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
    ipc::UnixSocket*,
    std::unique_ptr<ipc::UnixSocket> new_connection) {
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
	RecordReader* record_reader = new RecordReader();
  task_runner_->AddFileDescriptorWatch(
      fd, [fd, weak_this, record_reader]() mutable {
        if (!weak_this) {
          close(fd);
					delete record_reader;
          return;
        }

        ssize_t rd = record_reader->Read(fd);
        if (rd == -1)
          return;
        total_read += static_cast<size_t>(rd);
        if ((total_read / 10000000) != ((total_read - rd) / 10000000))
          PERFETTO_LOG("perfhd: %lu\n", total_read);
        if (rd == 0) {
          weak_this->task_runner_->RemoveFileDescriptorWatch(fd);
          close(fd);
					delete record_reader;
        }
      });
}

void PipeSender::OnDisconnect(ipc::UnixSocket* self) {
  socks_.erase(self);
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
