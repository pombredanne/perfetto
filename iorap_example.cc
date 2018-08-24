#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <thread>

#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/time.h"

#include "perfetto/config/trace_config.pb.h"
#include "perfetto/trace/trace.pb.h"

namespace {

int64_t t_start;

using perfetto::base::GetWallTimeNs;

struct PerfettoChild {
  pid_t pid;
  perfetto::base::ScopedFstream cfg_in;
  perfetto::base::ScopedFstream trace_out;
};

PerfettoChild SpawnPerfetto() {
  constexpr int kPipeReadEnd = 0;
  constexpr int kPipeWriteEnd = 1;

  int stdin_pipe[2]{};   // This is to push the trace config.
  int stdout_pipe[2]{};  // This is to get the trace data back.

  PERFETTO_CHECK(pipe(stdin_pipe) == 0);
  PERFETTO_CHECK(pipe(stdout_pipe) == 0);
  int devNullFd = open("/dev/null", O_WRONLY | O_CLOEXEC);

  auto pid = vfork();
  PERFETTO_CHECK(pid >= 0);
  if (pid != 0) {
    // main process
    close(stdin_pipe[kPipeReadEnd]);
    close(stdout_pipe[kPipeWriteEnd]);
    PerfettoChild spec;
    spec.pid = pid;
    spec.cfg_in.reset(fdopen(stdin_pipe[kPipeWriteEnd], "wb"));
    spec.trace_out.reset(fdopen(stdout_pipe[kPipeReadEnd], "rb"));
    return spec;
  }

  dup2(stdin_pipe[kPipeReadEnd], STDIN_FILENO);
  dup2(stdout_pipe[kPipeWriteEnd], STDOUT_FILENO);
  dup2(devNullFd, STDERR_FILENO);

  close(stdin_pipe[kPipeReadEnd]);
  close(stdin_pipe[kPipeWriteEnd]);
  close(stdout_pipe[kPipeReadEnd]);
  close(stdout_pipe[kPipeWriteEnd]);
  for (int i = 0; i < 1024; i++) {
    if (i != STDIN_FILENO && i != STDOUT_FILENO && i != STDERR_FILENO)
      close(i);  // TODO: This is silly, just use fcntl(SETCLOEXEC).
  }

  char tstamp[32];
  sprintf(tstamp, "%llu", GetWallTimeNs().count());
  // This needs to be /system/bin/perfetto.
  // alert-id is a hack to propagate the timestamp.
  execl("/data/local/tmp/perfetto", "perfetto", "--config", "-", "--out", "-",
        "--alert-id", tstamp, nullptr);

  // execl() doesn't return in case of success, if we get here something
  // failed.
  _exit(4);
}

void dump_time_into_trace() {
  perfetto::base::ScopedFile fd(open("/d/tracing/trace_marker", O_WRONLY));
  PERFETTO_CHECK(fd);
  for (int i = 0; i < 3000; i++) {
    char buf[32];
    sprintf(buf, "%llu", GetWallTimeNs().count());
    write(*fd, buf, strlen(buf));
    usleep(1000);
  }
}

}  // namespace

int main() {
  perfetto::protos::TraceConfig config;
  // Setup the trace config.
  {
    config.set_duration_ms(1000);
    config.add_buffers()->set_size_kb(1024 * 32);
    auto* data_source = config.add_data_sources();
    auto* ds_config = data_source->mutable_config();
    ds_config->set_name("linux.ftrace");
    auto* ftrace_config = ds_config->mutable_ftrace_config();

    // These should be configurable via Phenotype.
    ftrace_config->set_buffer_size_kb(8192);  // in-kernel ftrace buffer.
    ftrace_config->set_drain_period_ms(200);
    ftrace_config->add_ftrace_events("print");
  }
  std::string config_raw = config.SerializeAsString();

  std::thread thd(dump_time_into_trace);

  t_start = GetWallTimeNs().count();

  PerfettoChild child = SpawnPerfetto();

  // Write the config on stdin.
  fwrite(config_raw.data(), 1, config_raw.size(), *child.cfg_in);
  child.cfg_in.reset();

  int childStatus = 0;
  waitpid(child.pid, &childStatus, 0);
  if (!WIFEXITED(childStatus) || WEXITSTATUS(childStatus) != 0) {
    PERFETTO_FATAL(
        "Child process failed (0x%x) while calling the Perfetto client",
        childStatus);
  }
  PERFETTO_LOG("Perfetto done, reading trace from stdout");

  std::vector<char> raw_trace;
  size_t raw_trace_size = 0;
  while (true) {
    constexpr size_t kBlockSize = 4096;
    raw_trace.resize(raw_trace.size() + kBlockSize);
    auto rsize =
        fread(&raw_trace[raw_trace_size], 1, kBlockSize, *child.trace_out);
    if (rsize <= 0)
      break;
    raw_trace_size += rsize;
    raw_trace.resize(raw_trace_size);
  }

  child.trace_out.reset();

  perfetto::protos::Trace trace;
  PERFETTO_CHECK(
      trace.ParseFromArray(raw_trace.data(), static_cast<int>(raw_trace_size)));
  int64_t t_exec = 0;
  int64_t first_marker = 0;
  for (const auto& packet : trace.packet()) {
    if (packet.has_trace_config()) {
      t_exec = packet.trace_config().statsd_metadata().triggering_alert_id();
    }
    if (packet.has_ftrace_events()) {
      const auto& ftrace_bundle = packet.ftrace_events();
      for (const auto& ftrace_event : ftrace_bundle.event()) {
        if (ftrace_event.has_print()) {
          const auto& print_event = ftrace_event.print();
          int64_t ts = strtoll(print_event.buf().c_str(), nullptr, 10);
          if (first_marker == 0)
            first_marker = ts;
          else
            PERFETTO_CHECK(ts > first_marker);
        } else {
          PERFETTO_LOG("evt: %d", ftrace_event.event_case());
        }
      }
    }
  }

  PERFETTO_ILOG("fork latency: %.3f ms", (t_exec - t_start) / 1e6);
  PERFETTO_ILOG("end-to-end latency: %.3f ms", (first_marker - t_start) / 1e6);
  thd.join();
}
