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

#include "src/ftrace_reader/cpu_stats_parser.h"

#include "perfetto/base/string_splitter.h"
#include "perfetto/base/string_utils.h"

namespace perfetto {
namespace {

uint32_t ExtractInt(const char* s) {
  for (; *s != '\0'; s++) {
    if (*s == ':') {
      return static_cast<uint32_t>(atoi(s + 1));
    }
  }
  return 0;
}

double ExtractDouble(const char* s) {
  for (; *s != '\0'; s++) {
    if (*s == ':') {
      return strtod(s + 1, nullptr);
    }
  }
  return -1;
}

}  // namespace

bool DumpCpuStats(std::string text,
                  protos::pbzero::FtraceStats::FtraceCpuStats* stats) {
  if (text == "")
    return false;

  base::StringSplitter splitter(std::move(text), '\n');
  while (splitter.Next()) {
    if (base::StartsWith(splitter.cur_token(), "entries")) {
      stats->set_entries(ExtractInt(splitter.cur_token()));
    } else if (base::StartsWith(splitter.cur_token(), "overrun")) {
      stats->set_overrun(ExtractInt(splitter.cur_token()));
    } else if (base::StartsWith(splitter.cur_token(), "commit overrun")) {
      stats->set_commit_overrun(ExtractInt(splitter.cur_token()));
    } else if (base::StartsWith(splitter.cur_token(), "bytes")) {
      stats->set_bytes_read(ExtractInt(splitter.cur_token()));
    } else if (base::StartsWith(splitter.cur_token(), "oldest event ts")) {
      stats->set_oldest_event_ts(ExtractDouble(splitter.cur_token()));
    } else if (base::StartsWith(splitter.cur_token(), "now ts")) {
      stats->set_now_ts(ExtractDouble(splitter.cur_token()));
    } else if (base::StartsWith(splitter.cur_token(), "dropped events")) {
      stats->set_dropped_events(ExtractInt(splitter.cur_token()));
    } else if (base::StartsWith(splitter.cur_token(), "read events")) {
      stats->set_read_events(ExtractInt(splitter.cur_token()));
    }
  }

  return true;
}

bool DumpAllCpuStats(FtraceProcfs* ftrace, protos::pbzero::FtraceStats* stats) {
  for (size_t cpu = 0; cpu < ftrace->NumberOfCpus(); cpu++) {
    auto* cpu_stats = stats->add_cpu_stats();
    cpu_stats->set_cpu(static_cast<uint32_t>(cpu));
    if (!DumpCpuStats(ftrace->ReadCpuStats(cpu), cpu_stats))
      return false;
  }
  return true;
}

}  // namespace perfetto
