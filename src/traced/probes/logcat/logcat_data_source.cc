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

#include "src/traced/probes/logcat/logcat_data_source.h"

#include "perfetto/base/file_utils.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/optional.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/string_splitter.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/base/unix_socket.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/trace_writer.h"

#include "perfetto/trace/android/android_logcat.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {
constexpr uint32_t kMinPollRateMs = 100;
constexpr uint32_t kDefaultPollRateMs = 1000;
constexpr size_t kBufSize = base::kPageSize;
const char kLogTagsPath[] = "/system/etc/event-log-tags";

// From Android's liblog/include/log/log_read.h .
struct logger_entry_v4 {
  uint16_t len;       // length of the payload.
  uint16_t hdr_size;  // sizeof(struct logger_entry_v4).
  int32_t pid;        // generating process's pid.
  uint32_t tid;       // generating process's tid.
  uint32_t sec;       // seconds since Epoch.
  uint32_t nsec;      // nanoseconds.
  uint32_t lid;       // log id of the payload, bottom 4 bits currently.
  uint32_t uid;       // generating process's uid.
};

// Event types definition in the binary encoded format, from
// //system/core/liblog/include/log/log.h.
// Obviously these values don't match the values of the textual dictionary
// definition of the event format in //system/core/logcat/event.logtags , the
// latter are off by one (INT = 1 and so on)
enum AndroidEventLogType {
  EVENT_TYPE_INT = 0,
  EVENT_TYPE_LONG = 1,
  EVENT_TYPE_STRING = 2,
  EVENT_TYPE_LIST = 3,
  EVENT_TYPE_FLOAT = 4,
};

}  // namespace

LogcatDataSource::LogcatDataSource(DataSourceConfig ds_config,
                                   base::TaskRunner* task_runner,
                                   TracingSessionID session_id,
                                   std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, kTypeId),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      logcat_sock_(base::UnixSocketRaw::CreateInvalid()),
      weak_factory_(this) {
  const auto& cfg = ds_config.android_logcat_config();
  poll_rate_ms_ = cfg.poll_ms() ? cfg.poll_ms() : kDefaultPollRateMs;
  poll_rate_ms_ = std::max(kMinPollRateMs, poll_rate_ms_);

  std::vector<uint32_t> log_ids;
  if (cfg.log_ids_size() == 0) {
    // If no log id is specified, add the most popular ones.
    log_ids.push_back(AndroidLogcatConfig::AndroidLogcatLogId::LID_DEFAULT);
    log_ids.push_back(AndroidLogcatConfig::AndroidLogcatLogId::LID_SYSTEM);
    log_ids.push_back(AndroidLogcatConfig::AndroidLogcatLogId::LID_EVENTS);
    log_ids.push_back(AndroidLogcatConfig::AndroidLogcatLogId::LID_CRASH);
    log_ids.push_back(AndroidLogcatConfig::AndroidLogcatLogId::LID_KERNEL);
  } else {
    for (uint32_t id : cfg.log_ids())
      log_ids.push_back(id);
  }

  // Build a linear vector out of the tag filters and keep track of the string
  // boundaries. Once done, derive StringView(s) out of the vector. This is
  // to create a set<StringView> which is backed by contiguous chars.
  std::vector<std::pair<size_t, size_t>> tag_boundaries;
  for (const std::string& tag : cfg.filter_tags()) {
    const size_t begin = filter_tags_strbuf_.size();
    filter_tags_strbuf_.insert(filter_tags_strbuf_.end(), tag.begin(),
                               tag.end());
    const size_t end = filter_tags_strbuf_.size();
    tag_boundaries.emplace_back(begin, end - begin);
  }

  filter_tags_strbuf_.shrink_to_fit();
  // At this point pointers to |filter_tags_strbuf_| are stable.

  for (const auto& it : tag_boundaries)
    filter_tags_.emplace(&filter_tags_strbuf_[it.first], it.second);

  // Build the command string that will be sent to the logdr socket on Start().
  mode_ = "stream lids";
  for (auto it = log_ids.begin(); it != log_ids.end(); it++) {
    mode_ += it == log_ids.begin() ? "=" : ",";
    mode_ += std::to_string(*it);
  }

  min_prio_ = cfg.min_prio();
  buf_ = base::PagedMemory::Allocate(kBufSize);
}

LogcatDataSource::~LogcatDataSource() = default;

void LogcatDataSource::Start() {
  ParseEventLogDefinitions();

  static const char kLogcatSocket[] = "/dev/socket/logdr";
  logcat_sock_ = base::UnixSocketRaw::CreateMayFail(base::SockType::kSeqPacket);
  if (!logcat_sock_ || !logcat_sock_.Connect(kLogcatSocket)) {
    PERFETTO_PLOG("Could not connecto to %s", kLogcatSocket);
    return;
  }
  PERFETTO_DLOG("Starting logcat stream: %s", mode_.c_str());
  if (logcat_sock_.Send(mode_.data(), mode_.size()) <= 0) {
    PERFETTO_PLOG("send() failed on logcat socket %s", kLogcatSocket);
    return;
  }
  logcat_sock_.SetBlocking(false);

  // // TODO: we can't reliably get this sync depending on the buffer usage.
  // DNS. PERFETTO_LOG("perfetto_logcat_clock_sync CLOCK_BOOTTIME=%lld",
  //              base::GetWallTimeNs().count());
  Tick(/*post_next_task=*/true);  // DNS
}

void LogcatDataSource::Tick(bool post_next_task) {
  auto weak_this = weak_factory_.GetWeakPtr();
  if (post_next_task) {
    auto now_ms = base::GetWallTimeMs().count();
    task_runner_->PostDelayedTask(
        [weak_this] {
          if (weak_this)
            weak_this->Tick(/*post_next_task=*/true);
        },
        poll_rate_ms_ - (now_ms % poll_rate_ms_));
  }

  ssize_t rsize;
  TraceWriter::TracePacketHandle packet;
  protos::pbzero::AndroidLogcatPacket* logcat_packet = nullptr;
  size_t num_events = 0;
  bool stop = false;
  while (!stop && (rsize = logcat_sock_.Receive(buf_.Get(), kBufSize)) > 0) {
    num_events++;
    stats_.num_total++;
    // Don't hold the message loop for too long. If there are so many events
    // in the queue, stop at some point and parse the remaining ones in another
    // task (posted after this while loop).
    if (num_events > 500) {
      stop = true;
      task_runner_->PostTask([weak_this] {
        if (weak_this)
          weak_this->Tick(/*post_next_task=*/false);
      });
    }
    char* buf = reinterpret_cast<char*>(buf_.Get());
    PERFETTO_DCHECK(reinterpret_cast<uintptr_t>(buf) % 16 == 0);
    size_t hdr_size = reinterpret_cast<logger_entry_v4*>(buf)->hdr_size;
    if (hdr_size >= static_cast<size_t>(rsize)) {
      PERFETTO_DLOG("Invalid hdr_size in logcat message (%zu <= %zd)", hdr_size,
                    rsize);
      stats_.num_failed++;
      continue;
    }

    // In older versions of Android the logger_entry struct can contain less
    // entried. Copy that in a temporary struct, so that unset fields are
    // always zero-initialized.
    logger_entry_v4 entry{};
    memcpy(&entry, buf, hdr_size);
    buf += hdr_size;
    if (static_cast<ssize_t>(entry.len) > rsize) {
      PERFETTO_DLOG("Invalid len in logcat message (%hu <= %zd)", entry.len,
                    rsize);
      stats_.num_failed++;
      continue;
    }
    char* end = buf + entry.len;

    if (!packet) {
      // Lazily add the packet on the first message received.
      packet = writer_->NewTracePacket();
      packet->set_timestamp(
          static_cast<uint64_t>(base::GetBootTimeNs().count()));
      logcat_packet = packet->set_logcat();
    }

    int prio = 0;
    base::StringView tag;
    base::StringView msg;
    protos::pbzero::AndroidLogcatPacket::LogEvent* evt = nullptr;

    if (entry.lid == AndroidLogcatConfig::AndroidLogcatLogId::LID_EVENTS) {
      // Entries in the EVENTS buffer are special, they are binary encoded.
      // See https://developer.android.com/reference/android/util/EventLog.
      if (!ParseBinaryEvent(buf, end, logcat_packet, &evt)) {
        PERFETTO_DLOG("Failed to parse logcat binary event");
        stats_.num_failed++;
      }
      if (!evt) {
        // Parsing succeeded but the events was skipped due to filters.
        continue;
      }
    } else {
      // Format:
      // [Priority 1 byte] [ tag ] [ NUL ] [ message ]
      prio = *(buf++);

      // Skip if the user specified a min-priority filter in the config.
      if (prio < min_prio_) {
        stats_.num_skipped++;
        continue;
      }
      if (prio > 10) {
        PERFETTO_DLOG(
            "Skipping logcat event with suspiciously high priority %d", prio);
        stats_.num_failed++;
        continue;
      }

      // Find the NUL terminator that separates |tag| from |message|.
      char* str_end;
      for (str_end = buf; str_end < end && *str_end; str_end++) {
      }
      if (str_end >= end - 2) {
        // Looks like there is a tag-less message.
        msg = base::StringView(buf, static_cast<size_t>(str_end - buf));
      } else {
        tag = base::StringView(buf, static_cast<size_t>(str_end - buf));
        buf = str_end + 1;  // Move |buf| to the start of the message.
        size_t msg_len = static_cast<size_t>(end - buf);

        // Protobuf strings don't need the nul terminator. If the string is
        // null terminator, omit the terminator from the length.
        if (msg_len > 0 && *(end - 1) == '\0')
          msg_len--;
        msg = base::StringView(buf, msg_len);
      }
      buf = str_end;

      if (!filter_tags_.empty() && filter_tags_.count(tag) == 0) {
        stats_.num_skipped++;
        continue;
      }
    }

    evt = evt ? evt : logcat_packet->add_events();
    uint64_t ts = entry.sec * 1000000000ULL + entry.nsec;
    evt->set_timestamp(ts);
    evt->set_log_id(static_cast<protos::pbzero::AndroidLogcatLogId>(entry.lid));
    evt->set_pid(entry.pid);
    evt->set_tid(entry.tid);
    if (prio && prio < 16)  // TODO remove this. DNS.
      evt->set_prio(static_cast<protos::pbzero::AndroidLogcatPriority>(prio));
    if (!tag.empty())
      evt->set_tag(tag.data(), tag.size());
    if (!msg.empty())
      evt->set_message(msg.data(), msg.size());
  }  // while(logcat_sock_.Receive())
  PERFETTO_DLOG("Seen %zu logcat events", num_events);
}

bool LogcatDataSource::ParseBinaryEvent(
    const char* start,
    const char* end,
    protos::pbzero::AndroidLogcatPacket* packet,
    protos::pbzero::AndroidLogcatPacket::LogEvent** out_evt) {
  const char* buf = start;
  int32_t eid;
  if (end - start < static_cast<ptrdiff_t>(sizeof(eid)))
    return false;
  memcpy(&eid, buf, sizeof(eid));
  buf += sizeof(eid);

  // TODO test events with 0 arguments. DNS.

  const EventFormat* fmt = GetEventFormat(eid);
  if (!fmt) {
    // We got an event which doesn't have a corresponding entry in
    // /system/etc/event-log-tags. In most cases this is a bug in the App that
    // produced the event, which forgot to update the log tags dictionary.
    return false;
  }

  if (!filter_tags_.empty() &&
      filter_tags_.count(base::StringView(fmt->name)) == 0) {
    stats_.num_skipped++;
    return true;
  }

  auto* evt = packet->add_events();
  *out_evt = evt;
  evt->set_tag(fmt->name.c_str());
  size_t field_id = 0;

  // TODO make this safer. // DNS.
  while (buf < end) {
    char type = *(buf++);
    if (field_id >= fmt->fields.size())
      return evt;
    const char* field_name = fmt->fields[field_id].c_str();
    switch (type) {
      case EVENT_TYPE_INT: {
        int32_t value;
        memcpy(&value, buf, sizeof(value));
        auto* arg = evt->add_args();
        arg->set_name(field_name);
        arg->set_int_value(value);
        buf += sizeof(value);
        field_id++;
        break;
      }
      case EVENT_TYPE_LONG: {
        int64_t value;
        memcpy(&value, buf, sizeof(value));
        auto* arg = evt->add_args();
        arg->set_name(field_name);
        arg->set_int_value(value);
        buf += sizeof(value);
        field_id++;
        break;
      }
      case EVENT_TYPE_FLOAT: {
        double value;
        memcpy(&value, buf, sizeof(value));
        auto* arg = evt->add_args();
        arg->set_name(field_name);
        arg->set_real_value(value);
        buf += sizeof(value);
        field_id++;
        break;
      }
      case EVENT_TYPE_STRING: {
        uint32_t len;
        memcpy(&len, buf, 4);
        buf += sizeof(len);
        auto* arg = evt->add_args();
        arg->set_name(field_name);
        arg->set_string_value(buf, len);
        buf += len;
        field_id++;
        break;
      }
      case EVENT_TYPE_LIST: {
        buf++;
        if (field_id > 0) {
          // Lists are supported only as a top-level node. We stop parsing when
          // encountering a list as an inner field. The very few of them are not
          // interesting.
          return evt;
        }
        break;
      }
      default:
        PERFETTO_DLOG(
            "Skipping unklonwn logcat binary event of type %d for %s at pos "
            "%zd after parsing %zu fields",
            static_cast<int>(type), fmt->name.c_str(), buf - start, field_id);
        return true;
    }  // switch(type)
  }    // while(buf < end)
  return true;
}

void LogcatDataSource::Flush(FlushRequestID, std::function<void()> callback) {
  // Grab most recent entries.
  Tick(/*post_next_task=*/false);

  // Emit stats.
  {
    auto packet = writer_->NewTracePacket();
    packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
    auto* stats = packet->set_logcat()->set_stats();
    stats->set_num_total(stats_.num_total);
    stats->set_num_skipped(stats_.num_skipped);
    stats->set_num_failed(stats_.num_failed);
  }

  writer_->Flush(callback);
}

void LogcatDataSource::ParseEventLogDefinitions() {
  std::string event_log_tags = ReadEventLogDefinitions();
  for (base::StringSplitter ss(std::move(event_log_tags), '\n'); ss.Next();) {
    if (!ParseEventLogDefinitionLine(ss.cur_token(), ss.cur_token_size() + 1)) {
      PERFETTO_DLOG("Could not parse event log format: %s", ss.cur_token());
      continue;
    }
  }
}

bool LogcatDataSource::ParseEventLogDefinitionLine(char* line, size_t len) {
  base::StringSplitter tok(line, len, ' ');
  if (!tok.Next())
    return false;
  auto id = static_cast<uint32_t>(std::strtoul(tok.cur_token(), nullptr, 10));
  if (!tok.Next())
    return false;
  std::string name(tok.cur_token(), tok.cur_token_size());
  auto it = event_formats_.emplace(id, EventFormat{std::move(name), {}}).first;
  char* format = tok.cur_token() + tok.cur_token_size() + 1;
  if (format >= line + len || !*format || *format == '\n') {
    return true;
  }
  size_t format_len = len - static_cast<size_t>(format - line);

  // Parse the arg formats, e.g.:
  // (status|1|5),(health|1|5),(present|1|5),(plugged|1|5),(technology|3).
  // We don't really care neither about the field type nor its unit (the two
  // numbers after the |). The binary format re-states the type and we don't
  // currently propagate the unit at all.
  int tokens_seen = 0;
  int tokens_parsed = 0;
  for (base::StringSplitter field(format, format_len, ','); field.Next();) {
    tokens_seen++;
    if (field.cur_token_size() <= 2)
      continue;
    char* start = field.cur_token() + 1;
    base::StringSplitter parts(start, field.cur_token_size() - 1, '|');
    if (!parts.Next())
      continue;
    std::string field_name(parts.cur_token(), parts.cur_token_size());
    it->second.fields.emplace_back(std::move(field_name));
    tokens_parsed++;
  }
  return tokens_seen == tokens_parsed;
}

std::string LogcatDataSource::ReadEventLogDefinitions() {
  std::string contents;
  if (!base::ReadFile(kLogTagsPath, &contents)) {
    PERFETTO_PLOG("Failed to read %s", kLogTagsPath);
    return "";
  }
  return contents;
}

const LogcatDataSource::EventFormat* LogcatDataSource::GetEventFormat(
    int id) const {
  auto it = event_formats_.find(id);
  return it == event_formats_.end() ? nullptr : &it->second;
}

}  // namespace perfetto
