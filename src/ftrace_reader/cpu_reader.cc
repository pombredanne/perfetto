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

#include "cpu_reader.h"

#include <signal.h>

#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "proto_translation_table.h"

#include "perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "perfetto/trace/ftrace/print.pbzero.h"
#include "perfetto/trace/ftrace/sched_switch.pbzero.h"

#include "perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"

namespace perfetto {

namespace {

static bool ReadIntoString(const uint8_t* start,
                           const uint8_t* end,
                           size_t field_id,
                           protozero::ProtoZeroMessage* out) {
  for (const uint8_t* c = start; c < end; c++) {
    if (*c != '\0')
      continue;
    out->AppendBytes(field_id, reinterpret_cast<const char*>(start), c - start);
    return true;
  }
  return false;
}

using BundleHandle =
    protozero::ProtoZeroMessageHandle<protos::pbzero::FtraceEventBundle>;

const std::vector<bool> BuildEnabledVector(const ProtoTranslationTable& table,
                                           const std::set<std::string>& names) {
  std::vector<bool> enabled(table.largest_id() + 1);
  for (const std::string& name : names) {
    const Event* event = table.GetEventByName(name);
    if (!event)
      continue;
    enabled[event->ftrace_event_id] = true;
  }
  return enabled;
}

void SetBlocking(int fd, bool is_blocking) {
  int flags = fcntl(fd, F_GETFL, 0);
  flags = (is_blocking) ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
  PERFETTO_CHECK(fcntl(fd, F_SETFL, flags) == 0);
}

int64_t NowNs() {
  timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000000000L + now.tv_nsec;
}

// For further documentation of these constants see the kernel source:
// linux/include/linux/ring_buffer.h
// Some information about the values of these constants are exposed to user
// space at: /sys/kernel/debug/tracing/events/header_event
const uint32_t kTypeDataTypeLengthMax = 28;
const uint32_t kTypePadding = 29;
const uint32_t kTypeTimeExtend = 30;
const uint32_t kTypeTimeStamp = 31;

const size_t kPageSize = 4096;

struct PageHeader {
  uint64_t timestamp;
  uint32_t size;
  uint32_t : 24;
  uint32_t overwrite : 8;
};

struct EventHeader {
  uint32_t type_or_length : 5;
  uint32_t time_delta : 27;
};

struct TimeStamp {
  uint64_t tv_nsec;
  uint64_t tv_sec;
};

}  // namespace

EventFilter::EventFilter(const ProtoTranslationTable& table,
                         std::set<std::string> names)
    : enabled_ids_(BuildEnabledVector(table, names)),
      enabled_names_(std::move(names)) {}
EventFilter::~EventFilter() = default;

CpuReader::CpuReader(base::TaskRunner* task_runner,
                     FtraceController* ftrace_controller,
                     const ProtoTranslationTable* table,
                     size_t cpu,
                     base::ScopedFile trace_pipe_raw)
    : table_(table), cpu_(cpu), trace_pipe_raw_(std::move(trace_pipe_raw)) {
  int pipe_fds[2] = {};
  PERFETTO_CHECK(pipe(&pipe_fds[0]) == 0);
  staging_read_fd_.reset(pipe_fds[0]);
  staging_write_fd_.reset(pipe_fds[1]);
  PERFETTO_CHECK(staging_read_fd_ && staging_write_fd_);

  SetBlocking(*trace_pipe_raw_, true);
  SetBlocking(*staging_write_fd_, true);
  SetBlocking(*staging_read_fd_, false);

  pipe_worker_ =
      std::thread(&CpuReader::PipeThread, task_runner, ftrace_controller, cpu,
                  *trace_pipe_raw_, *staging_write_fd_);
}

CpuReader::~CpuReader() {
  // Closing the pipes should cause splice() to return EPIPE or EBADF.
  // TODO this code needs to be carefully checked.
  trace_pipe_raw_.reset();
  staging_read_fd_.reset();
  staging_write_fd_.reset();
  // pthread_kill(pipe_worker_.native_handle(), SIGPIPE);  // TODO why do I need
  // this?
  PERFETTO_DLOG("Cpu reader dtor... cpu:%zu", cpu_);
  pipe_worker_.join();
  // TODO: there seems to be a bug here, often cpu0 never joins.
  PERFETTO_DLOG("  dtor joined cpu:%zu", cpu_);
}

// static
void CpuReader::PipeThread(base::TaskRunner* task_runner,
                           FtraceController* ftrace_controller,
                           size_t cpu,
                           int trace_pipe_raw,
                           int staging_write_fd) {
  char thread_name[16];
  snprintf(thread_name, sizeof(thread_name), "traced_probes.%zu", cpu);
  pthread_setname_np(pthread_self(), thread_name);

  // Mask SIGPIPE. This will be raised when destroying the other end of the pipe
  // in the CpuReader dtor to force this thread to exit.
  // TODO: not 100% sure this is needed, not sure if signal(SIGPIPE, SIGIGN)
  // would be better.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGPIPE);
  sigprocmask(SIG_BLOCK, &set, nullptr);

  size_t pages_in_staging_pipe = 0;
  useconds_t throttle = 0;

  static constexpr useconds_t kThrottlePeriodHiRateUs = 50 * 1000L;
  static constexpr useconds_t kThrottlePeriodLowRateUs = 250 * 1000L;
  static constexpr useconds_t kWakeupThresholdNs = 100 * 1000L * 1000L;
  static constexpr size_t kPagesBurstForHiRateTransition = 10;

  for (;;) {
    if (throttle)
      usleep(throttle);

    // Do a blocking splice() first.
    size_t pages_spliced_in_cur_iteration = 0;
    int64_t t_start = NowNs();
    int splice_res = splice(trace_pipe_raw, nullptr, staging_write_fd, nullptr,
                            kPageSize, SPLICE_F_MOVE);
    if (splice_res < 0 && (errno == EPIPE || errno == EBADF))  // dtor called
      break;

    const int64_t blocking_splice_duration_ns = NowNs() - t_start;
    PERFETTO_DCHECK(splice_res == kPageSize);
    pages_spliced_in_cur_iteration++;

    // Move as many pages as we can from the trace_pipe_raw to the staging pipe
    // until either the former is empty or the latter is full.
    for (;;) {
      splice_res = splice(trace_pipe_raw, nullptr, staging_write_fd, nullptr,
                          kPageSize, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
      if (splice_res > 0) {
        pages_spliced_in_cur_iteration++;
        PERFETTO_DCHECK(splice_res == kPageSize);
      } else {
        break;
      }
    }
    if (splice_res < 0 && (errno == EPIPE || errno == EBADF))  // dtor called
      break;
    PERFETTO_DCHECK(splice_res < 0 && errno == EAGAIN);

    if (pages_spliced_in_cur_iteration >= kPagesBurstForHiRateTransition) {
      // We seem to be under a tracing burst. We can't just splice() straight
      // away as doing so will interfere too much with the CPU run queues and
      // introduce too much overhead. At the same time we can't be too relaxed
      // because we are at serious risk of dropping content from the kernel
      // ftrace buffer.  Throttle at kThrottlePeriodHiRateUs.
      PERFETTO_DLOG("Entering burst mode");
      throttle = kThrottlePeriodHiRateUs;
    } else if (blocking_splice_duration_ns < kWakeupThresholdNs) {
      // We have a manageable backlog in the staging pipe, but enough tracing
      // activity that wakes us up too frequently. Introduce some throttling to
      // relax the wakeups.
      // TODO this is true only if splice is retuning because the output pipe is
      // not full. We should figure out a way to distinguish the two cases.
      throttle = kThrottlePeriodLowRateUs;
    } else {
      // We don't seem to be doing too bad. Just let the blocking splice() set
      // our pace.
      throttle = 0;
    }

    pages_in_staging_pipe += pages_spliced_in_cur_iteration;

    PERFETTO_DLOG("Splice[%zu], pages: %zu / %zu, ms: %" PRIi64 " throttle: %u",
                  cpu, pages_spliced_in_cur_iteration, pages_in_staging_pipe,
                  blocking_splice_duration_ns / 1000000, throttle);

    // TODO this 8 below is somewhat arbitrary. Careful with it though,
    // this heavily relies on the default pipe buffer being > 10 * 4096.
    if (pages_in_staging_pipe > 8) {
      task_runner->PostTask([ftrace_controller, cpu] {
        ftrace_controller->OnRawFtraceDataAvailable(cpu);
      });
      pages_in_staging_pipe = 0;
    }
  }
  PERFETTO_DLOG("Quitting splice thread for cpu %zu", cpu);
}

bool CpuReader::Drain(const std::array<const EventFilter*, kMaxSinks>& filters,
                      const std::array<BundleHandle, kMaxSinks>& bundles) {
  if (!staging_read_fd_)
    return false;
  uint8_t* buffer = GetBuffer();

  // TODO loop outer, if looping here something breaks in trace_writer_impl.
  // for (;;)
  {
    long bytes =
        PERFETTO_EINTR(read(staging_read_fd_.get(), buffer, kPageSize));
    if (bytes == -1 && errno == EAGAIN)
      return false;
    // TODO: think about other error conditions.
    PERFETTO_CHECK(static_cast<size_t>(bytes) == kPageSize);
    size_t evt_size = 0;
    for (size_t i = 0; i < kMaxSinks; i++) {
      if (!filters[i])
        break;
      evt_size = ParsePage(cpu_, buffer, filters[i], &*bundles[i], table_);
      PERFETTO_DCHECK(evt_size);
    }
    return true;
  }
}

uint8_t* CpuReader::GetBuffer() {
  // TODO(primiano): Guard against overflows, like BufferedFrameDeserializer.
  if (!buffer_)
    buffer_ = base::PageAllocator::Allocate(kPageSize);
  return static_cast<uint8_t*>(buffer_.get());
}

// The structure of a raw trace buffer page is as follows:
// First a page header:
//   8 bytes of timestamp
//   8 bytes of page length TODO(hjd): other fields also defined here?
// // TODO(hjd): Document rest of format.
// Some information about the layout of the page header is available in user
// space at: /sys/kernel/debug/tracing/events/header_event
// This method is deliberately static so it can be tested independently.
size_t CpuReader::ParsePage(size_t cpu,
                            const uint8_t* ptr,
                            const EventFilter* filter,
                            protos::pbzero::FtraceEventBundle* bundle,
                            const ProtoTranslationTable* table) {
  const uint8_t* const start_of_page = ptr;
  const uint8_t* const end_of_page = ptr + kPageSize;

  bundle->set_cpu(cpu);

  // TODO(hjd): Read this format dynamically?
  PageHeader page_header;
  if (!ReadAndAdvance(&ptr, end_of_page, &page_header))
    return 0;

  // TODO(hjd): There is something wrong with the page header struct.
  page_header.size = page_header.size & 0xfffful;

  const uint8_t* const end = ptr + page_header.size;
  if (end > end_of_page)
    return 0;

  uint64_t timestamp = page_header.timestamp;

  while (ptr < end) {
    EventHeader event_header;
    if (!ReadAndAdvance(&ptr, end, &event_header))
      return 0;

    timestamp += event_header.time_delta;

    switch (event_header.type_or_length) {
      case kTypePadding: {
        // Left over page padding or discarded event.
        if (event_header.time_delta == 0) {
          // TODO(hjd): Look at the next few bytes for read size;
          PERFETTO_CHECK(false);  // TODO(hjd): Handle
        }
        uint32_t length;
        if (!ReadAndAdvance<uint32_t>(&ptr, end, &length))
          return 0;
        ptr += length;
        break;
      }
      case kTypeTimeExtend: {
        // Extend the time delta.
        uint32_t time_delta_ext;
        if (!ReadAndAdvance<uint32_t>(&ptr, end, &time_delta_ext))
          return 0;
        // See https://goo.gl/CFBu5x
        timestamp += ((uint64_t)time_delta_ext) << 27;
        break;
      }
      case kTypeTimeStamp: {
        // Sync time stamp with external clock.
        TimeStamp time_stamp;
        if (!ReadAndAdvance<TimeStamp>(&ptr, end, &time_stamp))
          return 0;
        // TODO(hjd): Handle.
        break;
      }
      // Data record:
      default: {
        PERFETTO_CHECK(event_header.type_or_length <= kTypeDataTypeLengthMax);
        // type_or_length is <=28 so it represents the length of a data record.
        if (event_header.type_or_length == 0) {
          // TODO(hjd): Look at the next few bytes for real size.
          PERFETTO_CHECK(false);
        }
        const uint8_t* start = ptr;
        const uint8_t* next = ptr + 4 * event_header.type_or_length;

        uint16_t ftrace_event_id;
        if (!ReadAndAdvance<uint16_t>(&ptr, end, &ftrace_event_id))
          return 0;
        if (filter->IsEventEnabled(ftrace_event_id)) {
          protos::pbzero::FtraceEvent* event = bundle->add_event();
          event->set_timestamp(timestamp);
          if (!ParseEvent(ftrace_event_id, start, next, table, event))
            return 0;
        }

        // Jump to next event.
        ptr = next;
      }
    }
  }
  return static_cast<size_t>(ptr - start_of_page);
}

// |start| is the start of the current event.
// |end| is the end of the buffer.
bool CpuReader::ParseEvent(uint16_t ftrace_event_id,
                           const uint8_t* start,
                           const uint8_t* end,
                           const ProtoTranslationTable* table,
                           protozero::ProtoZeroMessage* message) {
  PERFETTO_DCHECK(start < end);
  const size_t length = end - start;

  // TODO(hjd): Rework to work even if the event is unknown.
  const Event& info = *table->GetEventById(ftrace_event_id);

  // TODO(hjd): Test truncated events.
  // If the end of the buffer is before the end of the event give up.
  if (info.size > length) {
    PERFETTO_DCHECK(false);
    return false;
  }

  bool success = true;
  for (const Field& field : table->common_fields())
    success &= ParseField(field, start, end, message);

  protozero::ProtoZeroMessage* nested =
      message->BeginNestedMessage<protozero::ProtoZeroMessage>(
          info.proto_field_id);

  for (const Field& field : info.fields)
    success &= ParseField(field, start, end, nested);

  // This finalizes |nested| automatically.
  message->Finalize();
  return success;
}

// Caller must guarantee that the field fits in the range,
// explicitly: start + field.ftrace_offset + field.ftrace_size <= end
// The only exception is fields with strategy = kCStringToString
// where the total size isn't known up front. In this case ParseField
// will check the string terminates in the bounds and won't read past |end|.
bool CpuReader::ParseField(const Field& field,
                           const uint8_t* start,
                           const uint8_t* end,
                           protozero::ProtoZeroMessage* message) {
  PERFETTO_DCHECK(start + field.ftrace_offset + field.ftrace_size <= end);
  const uint8_t* field_start = start + field.ftrace_offset;
  uint32_t field_id = field.proto_field_id;

  switch (field.strategy) {
    case kUint32ToUint32:
    case kUint32ToUint64:
      ReadIntoVarInt<uint32_t>(field_start, field_id, message);
      return true;
    case kUint64ToUint64:
      ReadIntoVarInt<uint64_t>(field_start, field_id, message);
      return true;
    case kInt32ToInt32:
    case kInt32ToInt64:
      ReadIntoVarInt<int32_t>(field_start, field_id, message);
      return true;
    case kInt64ToInt64:
      ReadIntoVarInt<int64_t>(field_start, field_id, message);
      return true;
    case kFixedCStringToString:
      // TODO(hjd): Add AppendMaxLength string to protozero.
      return ReadIntoString(field_start, field_start + field.ftrace_size,
                            field_id, message);
    case kCStringToString:
      // TODO(hjd): Kernel-dive to check this how size:0 char fields work.
      return ReadIntoString(field_start, end, field.proto_field_id, message);
  }
  // Not reached, for gcc.
  PERFETTO_CHECK(false);
  return false;
}

}  // namespace perfetto
