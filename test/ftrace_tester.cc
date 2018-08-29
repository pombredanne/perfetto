#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <thread>
#include <algorithm>

#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/time.h"
#include "perfetto/base/utils.h"

namespace perfetto {
namespace {

const char kTracing[] = "/sys/kernel/debug/tracing/";

void ClearFile(const std::string& suffix) {
  base::ScopedFile fd = base::OpenFile(kTracing + suffix, O_WRONLY | O_TRUNC);
  PERFETTO_CHECK(fd);
}

int64_t GetTime() {
  struct timespec ts;
  PERFETTO_CHECK(clock_gettime(CLOCK_BOOTTIME, &ts) != -1);
  return base::FromPosixTimespec(ts).count();
}

void WriteToFile(const std::string& suffix, const std::string& str) {
  base::ScopedFile fd = base::OpenFile(kTracing + suffix, O_WRONLY);
  PERFETTO_CHECK(fd);
  ssize_t written = PERFETTO_EINTR(write(fd.get(), str.c_str(), str.length()));
  ssize_t length = static_cast<ssize_t>(str.length());
  PERFETTO_CHECK(written == length || written == -1);
}


static void DumpTimeIntoTrace() {
  std::string path = std::string(kTracing) + "trace_marker";
  perfetto::base::ScopedFile fd;

  for (int i = 0; i < 100; i++) {
    char buf[32];
    if (!fd)
      fd.reset(open(path.c_str(), O_WRONLY));
    if (!fd)
      continue;
    PERFETTO_CHECK(fd);
    sprintf(buf, "%" PRIu64, GetTime());
    write(*fd, buf, strlen(buf));
    usleep(100);
  }
}

void FtraceTester(int argc, char** argv) {
    enum LongOption {
    OPT_CLEAR_TRACE = 1000,
    OPT_CLEAR_PER_CPU,
    OPT_BUFFER_SIZE,
    OPT_SET_BUFFER_SIZE,
  };
  static const struct option long_options[] = {
    // |option_index| relies on the order of options, don't reshuffle them.
    {"verbose", no_argument, nullptr, 'v'},
    {"buffer", required_argument, nullptr, OPT_BUFFER_SIZE},
    {"clear",  no_argument, nullptr, OPT_CLEAR_TRACE},
    {"clear-cpus", no_argument, nullptr, OPT_CLEAR_PER_CPU},
    {"set-buffer", no_argument, nullptr, OPT_SET_BUFFER_SIZE},
    {nullptr, 0, nullptr, 0}};

  int option_index = 0;
  const char* kBufferSize = "4096";
  bool kClearTrace = false;
  bool kClearPerCpu = false;
  bool kChangeBufferSize = false;
  bool kShowBreakdown = false;

  for (;;) {
    int option =
        getopt_long(argc, argv, "v:", long_options, &option_index);

    if (option == -1)
      break;  // EOF.

    if (option == 'v')
      kShowBreakdown = true;
    if (option == OPT_SET_BUFFER_SIZE)
      kChangeBufferSize = true;
    if (option == OPT_CLEAR_TRACE)
      kClearTrace = true;
    if (option == OPT_CLEAR_PER_CPU)
      kClearPerCpu = true;
    if (option == OPT_BUFFER_SIZE)
      kBufferSize = optarg;
  }

  WriteToFile("trace_clock", "boot");
  WriteToFile("buffer_size_kb", kBufferSize);

  WriteToFile("tracing_on", "0");
  ClearFile("trace");

  for (int rep = 0; rep < 20; rep++) {
    std::thread td(DumpTimeIntoTrace);
    int64_t start = GetTime();
    //WriteToFile("events/sched/sched_switch/enable", "1");
    if (kClearTrace) {
      ClearFile("trace");
    }
    if (kClearPerCpu) {
      for (int i = 0; i < 8; i++) {
        char path[32];
        sprintf(path, "per_cpu/cpu%d/trace", i);
        ClearFile(path);
      }
    }
    int64_t cleared = GetTime();
    if (kChangeBufferSize) {
      WriteToFile("buffer_size_kb", kBufferSize);
    }
    int64_t set_buffer = GetTime();
    WriteToFile("tracing_on", "1");
    int64_t enabled = GetTime();

    usleep(4000);

    base::ScopedFstream tf(fopen((std::string(kTracing) + "trace").c_str(), "r"));
    PERFETTO_CHECK(tf);
    char buf[1024];
    int64_t min_timestamp_ns = std::numeric_limits<int64_t>::max();
    while (fgets(buf, sizeof(buf) - 1, *tf)) {
      if (buf[0] == '#')
        continue;
      std::string line(buf);

      static const char kMarker[] = "tracing_mark_write: ";
      auto lhs = line.find(kMarker);
      if (lhs == std::string::npos)
        continue;
      lhs += strlen(kMarker);
      auto rhs = line.find("\n", lhs);
      if (rhs == std::string::npos)
        continue;
      line = line.substr(lhs, rhs-lhs);
      int64_t timestamp_ns = std::stoll(line);
      min_timestamp_ns = std::min(min_timestamp_ns, timestamp_ns);
    }


    if (kShowBreakdown) {
      printf("  Clearing: %.2fms\n", (cleared - start) / 1e6);
      printf("Set buffer: %.2fms\n", (set_buffer - cleared) / 1e6);
      printf("  Enabling: %.2fms\n", (enabled - set_buffer) / 1e6);
      printf("     Other: %.2fms\n", (min_timestamp_ns - enabled) / 1e6);
      printf("     Total: %.2fms\n\n", (min_timestamp_ns - start) / 1e6);
    } else {
      printf("%.2fms\n", (min_timestamp_ns - start) / 1e6);
    }

    // Clean up
    WriteToFile("events/sched/sched_switch/enable", "0");
    WriteToFile("tracing_on", "0");
    ClearFile("trace");
    if (kChangeBufferSize) {
      WriteToFile("buffer_size_kb", "0");
    }
    td.join();
  }
}

} // namespace
} // namespace perfetto

int main(int argc, char** argv) {
  perfetto::FtraceTester(argc, argv);
  return 0;
}
