#include <emscripten/emscripten.h>
#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <list>
#include <map>
#include <utility>

#include "perfetto/trace/trace.pb.h"

using namespace perfetto;

extern "C" uint32_t FetchTrace(uint32_t offset, uint32_t length);

extern "C" void EMSCRIPTEN_KEEPALIVE PerfettoLoadTrace();
extern "C" void EMSCRIPTEN_KEEPALIVE PerfettoOnTraceFetched(const char* data,
                                                            uint32_t size);

// -----------------------------------------------------------------------------
// TraceStorage
// -----------------------------------------------------------------------------
class TraceStorage {
 public:
  static TraceStorage* GetInstance();

  using FetchChunkCallback =
      std::function<void(const char* /*data*/, uint32_t /*size*/)>;
  void FetchChunk(uint32_t offset, uint32_t size, FetchChunkCallback);

  void OnChunkFetched(const char* data, uint32_t size);

 private:
  struct PendingFetch {
    uint32_t offset;
    uint32_t size;
    FetchChunkCallback callback;
  };

  void MaybeScheduleNextFetch();
  std::list<PendingFetch> pending_fetches_;
  bool fetch_scheduled_ = false;
};

TraceStorage* TraceStorage::GetInstance() {
  static TraceStorage* instance = new TraceStorage();
  return instance;
}

void TraceStorage::FetchChunk(uint32_t offset,
                              uint32_t size,
                              FetchChunkCallback callback) {
  pending_fetches_.emplace_back(
      PendingFetch{offset, size, std::move(callback)});
  if (pending_fetches_.size() == 1)
    MaybeScheduleNextFetch();
}

void TraceStorage::MaybeScheduleNextFetch() {
  if (pending_fetches_.empty() || fetch_scheduled_)
    return;
  const auto& job = pending_fetches_.front();
  fetch_scheduled_ = true;
  FetchTrace(static_cast<uint32_t>(job.offset), job.size);
}

void TraceStorage::OnChunkFetched(const char* data, uint32_t size) {
  if (pending_fetches_.empty())
    abort();
  fetch_scheduled_ = false;
  const auto& job = pending_fetches_.front();
  auto callback = std::move(job.callback);
  pending_fetches_.pop_front();
  callback(data, size);
  MaybeScheduleNextFetch();
}

// -----------------------------------------------------------------------------
// TraceIndexer
// -----------------------------------------------------------------------------

class FtraceIndex {
 public:
  using TimeNs = uint64_t;
  using OffsetBytes = uint32_t;
  static constexpr OffsetBytes kInvalidOffset =
      std::numeric_limits<OffsetBytes>::max();

  void AddPacket(const protos::FtraceEventBundle&, OffsetBytes);

  // Returns the offset of the a packet that starts <= TimeNs and that is
  // reasonably close to it.
  OffsetBytes Lookup(TimeNs);

 private:
  std::map<TimeNs, OffsetBytes> index_;
  uint32_t tot_events_ = 0;
};

void FtraceIndex::AddPacket(const protos::FtraceEventBundle& ftrace_bundle,
                            OffsetBytes offset_bytes) {
  if (ftrace_bundle.event_size() <= 0)
    return;
  tot_events_ += static_cast<uint32_t>(ftrace_bundle.event_size());
  TimeNs timestamp = ftrace_bundle.event(0).timestamp();
  // uint32_t cpu = ftrace_bundle.cpu();
  index_.emplace(timestamp, offset_bytes);
}

FtraceIndex::OffsetBytes FtraceIndex::Lookup(TimeNs ns) {
  auto it = index_.upper_bound(ns);
  if (it == index_.end())
    return kInvalidOffset;
  if (it != index_.begin())
    it--;
  return it->second;
}

// -----------------------------------------------------------------------------
// TraceLoader
// -----------------------------------------------------------------------------

class TraceLoader {
 public:
  void IndexFullTrace();
  void IndexPacket(const protos::TracePacket&, uint32_t offset);
  void FetchNextChunk();
  void OnChunkFetched(const char* data, uint32_t size);

 private:
  static constexpr uint32_t kChunkSize = 1024 * 1024;
  uint32_t cur_offset_ = 0;
  FtraceIndex ftrace_index_;  // TODO: should be per CPU.
};

void TraceLoader::IndexFullTrace() {
  cur_offset_ = 0;
  FetchNextChunk();
}

void TraceLoader::IndexPacket(const protos::TracePacket& packet, uint32_t offset) {
  switch (packet.data_case()) {
    case protos::TracePacket::kFtraceEvents:
      ftrace_index_.AddPacket(packet.ftrace_events(), offset);
      break;

    case protos::TracePacket::DATA_NOT_SET:
    case protos::TracePacket::kProcessTree:
    case protos::TracePacket::kInodeFileMap:
    case protos::TracePacket::kChromeEvents:
    case protos::TracePacket::kClockSnapshot:
    case protos::TracePacket::kTraceConfig:
    case protos::TracePacket::kForTesting:
      break;
  }
}

void TraceLoader::FetchNextChunk() {
  TraceStorage::GetInstance()->FetchChunk(
      cur_offset_, kChunkSize,
      [this](const char* data, uint32_t size) { OnChunkFetched(data, size); });
}

void TraceLoader::OnChunkFetched(const char* data, uint32_t size) {
  // printf("Fetched %u @ %u\n", size, cur_offset_);

  if (size == 0)
    return;

  const char* p = data;

  auto bytes_left = [data, size, &p]() -> uint32_t {
    return size - static_cast<uint32_t>(p - data);
  };

  for (;;) {
    if (bytes_left() < 5)
      break;

    const char* last_packet_start = p;

    // TOOD check bytes left > 5.
    assert(*(p++) == 0x0a);  // Field ID:1, type:length delimited.

    // Read the varint size.
    uint32_t packet_size = 0;
    uint32_t shift = 0;
    for (;;) {
      char c = *(p++);
      packet_size |= static_cast<uint32_t>(c & 0x7f) << shift;
      shift += 7;
      if (!(c & 0x80))
        break;
    }

    if (bytes_left() < packet_size) {
      p = last_packet_start;
      break;
    }

    protos::TracePacket packet;
    if (!packet.ParseFromArray(p, static_cast<int>(packet_size))) {
      printf("Failed to read packet, left: %u\n", bytes_left());
      abort();
    }
    IndexPacket(packet, cur_offset_ + static_cast<uint32_t>(p - data));
    // printf("Read packet @ %u, len: %u\n",
           // cur_offset_ + static_cast<uint32_t>(p - data), packet_size);
    p += packet_size;
  }

  cur_offset_ += static_cast<uint32_t>(p - data);
  FetchNextChunk();
}

// -----------------------------------------------------------------------------
// Functions exported to JS
// -----------------------------------------------------------------------------

extern "C" void PerfettoLoadTrace() {
  static TraceLoader* trace_loader = new TraceLoader();
  trace_loader->IndexFullTrace();
}

extern "C" void PerfettoOnTraceFetched(const char* data, uint32_t size) {
  TraceStorage::GetInstance()->OnChunkFetched(data, size);
}

int main() {
  printf("WASM runtime ready\n");
  return 0;
}
// extern "C" void PerfettoProcessTrace(const char* mem, int size) {
//   printf("Parsing trace in C++ (mem: %p, size: %d)\n",
//          reinterpret_cast<const void*>(mem), size);
//
//   char* alloced = static_cast<char*>(malloc(1024));
//   strcpy(alloced, "uninitialized");
//
//   protos::Trace trace;
//   bool parsed = trace.ParseFromArray(mem, size);
//   printf("Parsed: %d, packets: %d\n", parsed, trace.packet_size());
//
//   std::map<std::string, int> instances;
//   for (int i = 0; i < trace.packet_size(); i++) {
//     const protos::TracePacket& packet = trace.packet(i);
//     if (!packet.has_ftrace_events())
//       continue;
//     const protos::FtraceEventBundle& bundle = packet.ftrace_events();
//     for (const protos::FtraceEvent& event : bundle.event()) {
//       if (!event.has_sched_switch())
//         continue;
//       const auto& ss = event.sched_switch();
//       instances[ss.prev_comm()]++;
//       instances[ss.next_comm()]++;
//     }
//   }
//   for (const auto& kv : instances)
//     printf("  %-18s: %d instances\n", kv.first.c_str(), kv.second);
//   printf("\nPROCESSING DONE\n");
// }
