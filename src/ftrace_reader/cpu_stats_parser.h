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

#ifndef SRC_FTRACE_READER_CPU_STATS_PARSER_H_
#define SRC_FTRACE_READER_CPU_STATS_PARSER_H_

#include <stddef.h>

#include <string>

#include "perfetto/trace/ftrace/ftrace_stats.pbzero.h"
#include "src/ftrace_reader/ftrace_procfs.h"

namespace perfetto {

bool DumpCpuStats(std::string text,
                  protos::pbzero::FtraceStats::FtraceCpuStats* stats);
bool DumpAllCpuStats(FtraceProcfs* ftrace, protos::pbzero::FtraceStats* stats);

}  // namespace perfetto

#endif  // SRC_FTRACE_READER_CPU_STATS_PARSER_H_
