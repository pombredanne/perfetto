#include <emscripten/emscripten.h>
#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <list>
#include <map>
#include <utility>

#include "perfetto/trace/trace.pb.h"

using namespace perfetto;

namespace {
constexpr uint32_t kIOBufSize = 1 * 1024 * 1024;
}  // namespace.

// Imports (functions defined in JS)
extern "C" uint32_t FetchTrace(uint32_t offset, uint32_t length);
extern "C" void TraceStatusUpdate(uint32_t bytes_loaded,
                                  bool complete,
                                  float duration_ms);

// Exports (functions exported to JS)
extern "C" void EMSCRIPTEN_KEEPALIVE PerfettoLoadTrace();
extern "C" void EMSCRIPTEN_KEEPALIVE PerfettoOnTraceFetched(uint32_t size);
extern "C" uint32_t EMSCRIPTEN_KEEPALIVE LookupFtracePacket(float timestamp_ms);
extern "C" void EMSCRIPTEN_KEEPALIVE PrintPacketsAt(float timestamp_ms);
extern "C" char* EMSCRIPTEN_KEEPALIVE GetIOBuf();
extern "C" uint32_t EMSCRIPTEN_KEEPALIVE GetIOBufSize();

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
// FtraceIndex
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

  TimeNs front() const { return index_.empty() ? 0 : index_.begin()->first; }
  TimeNs back() const { return index_.empty() ? 0 : index_.rbegin()->first; }

  float duration() const {
    if (index_.empty())
      return 0;
    TimeNs duration = index_.rbegin()->first - index_.begin()->first;
    return duration / 1000000000.0f;
  }

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
  static TraceLoader* GetInstance();
  static uint32_t DecodePacket(const char*, size_t, protos::TracePacket*);

  void IndexFullTrace();
  void IndexPacket(const protos::TracePacket&, uint32_t offset);
  void FetchNextChunk();
  void OnChunkFetched(const char* data, uint32_t size);

  FtraceIndex* ftrace_index() { return &ftrace_index_; }
  uint32_t bytes_processed() const { return cur_offset_; }

 private:
  uint32_t cur_offset_ = 0;
  FtraceIndex ftrace_index_;  // TODO: should be per CPU.
};

TraceLoader* TraceLoader::GetInstance() {
  static TraceLoader* instance = new TraceLoader();
  return instance;
}

void TraceLoader::IndexFullTrace() {
  cur_offset_ = 0;
  FetchNextChunk();
}

void TraceLoader::IndexPacket(const protos::TracePacket& packet,
                              uint32_t offset) {
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
      cur_offset_, kIOBufSize,
      [this](const char* data, uint32_t size) { OnChunkFetched(data, size); });
}

uint32_t TraceLoader::DecodePacket(const char* data,
                                   size_t len,
                                   protos::TracePacket* packet) {
  if (len < 5)
    return 0;

  const char* p = data;
  if (*(p++) != 0x0a)  // Field ID:1, type:length delimited.
    abort();

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

  if (len < packet_size)
    return 0;

  // printf("Decoding packet %p - %u\n", reinterpret_cast<const void*>(p),
  //        packet_size);
  // for (uint32_t i = 0; i < packet_size; i++)
  //   printf("%02x ", p[i] & 0xff);
  // printf("\n");
  if (!packet->ParseFromArray(p, static_cast<int>(packet_size))) {
    printf("Failed to read packet %p - %u\n", reinterpret_cast<const void*>(p),
           packet_size);
    abort();
  }
  return static_cast<uint32_t>(p - data) + packet_size;
}

void TraceLoader::OnChunkFetched(const char* data, uint32_t size) {
  // printf("Fetched %u @ %u\n", size, cur_offset_);

  bool complete = size == 0;
  TraceStatusUpdate(cur_offset_, complete, ftrace_index_.duration());
  if (complete)
    return;

  const char* p = data;

  for (;;) {
    uint32_t bytes_left = size - static_cast<uint32_t>(p - data);
    protos::TracePacket packet;
    uint32_t packet_size = TraceLoader::DecodePacket(p, bytes_left, &packet);
    if (packet_size == 0)
      break;
    IndexPacket(packet, cur_offset_ + static_cast<uint32_t>(p - data));
    p += packet_size;
  }

  cur_offset_ += static_cast<uint32_t>(p - data);
  FetchNextChunk();
}

// -----------------------------------------------------------------------------
// Functions exported to JS
// -----------------------------------------------------------------------------

extern "C" void PerfettoLoadTrace() {
  TraceLoader::GetInstance()->IndexFullTrace();
}

extern "C" void PerfettoOnTraceFetched(uint32_t size) {
  TraceStorage::GetInstance()->OnChunkFetched(GetIOBuf(), size);
}

extern "C" uint32_t LookupFtracePacket(float timestamp_ms) {
  FtraceIndex* fi = TraceLoader::GetInstance()->ftrace_index();
  uint64_t ns = fi->front() + static_cast<uint64_t>(timestamp_ms * 1000000);
  return fi->Lookup(ns);
}

extern "C" void PrintPacketsAt(float timestamp_ms) {
  FtraceIndex* fi = TraceLoader::GetInstance()->ftrace_index();
  uint64_t ns = fi->front() + static_cast<uint64_t>(timestamp_ms * 1000000);
  uint32_t off = fi->Lookup(ns);
  printf("Printing packets @ T = %u (file offset: %u)\n\n",
         static_cast<uint32_t>(timestamp_ms), off);
  TraceStorage::GetInstance()->FetchChunk(
      off, 1024 * 64, [](const char* data, uint32_t size) {
        protos::TracePacket packet;
        uint32_t res = TraceLoader::DecodePacket(data, size, &packet);
        if (!res) {
          printf("Failed to parse packet\n");
          return;
        }
        uint64_t trace_start =
            TraceLoader::GetInstance()->ftrace_index()->front();
        const protos::FtraceEventBundle& bundle = packet.ftrace_events();
        for (const protos::FtraceEvent& event : bundle.event()) {
          if (!event.has_sched_switch())
            continue;
          const auto& ss = event.sched_switch();
          printf("%-9.2lf ms: %s -> %s\n",
                 (event.timestamp() - trace_start) / 1000000.0,
                 ss.prev_comm().c_str(), ss.next_comm().c_str());
        }
      });
}

extern "C" char* GetIOBuf() {
  static char* iobuf = static_cast<char*>(malloc(kIOBufSize));
  return iobuf;
}

extern "C" uint32_t GetIOBufSize() {
  return kIOBufSize;
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
