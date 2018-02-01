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

#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
//#include <dirent.h>

#include "ftrace_proto_gen.h"
#include "perfetto/base/logging.h"
#include "perfetto/ftrace_reader/format_parser.h"

int main(int argc, const char** argv) {
  if (argc != 4) {
    printf("Usage: ./%s in.format out.proto\n", argv[0]);
    return 1;
  }

  const char* whitelist_path = argv[1];
  const char* input_dir = argv[2];
  const char* output_dir = argv[3];

  std::set<std::string> events = perfetto::GetWhitelistedEvents(whitelist_path);
  std::vector<std::string> events_info;

  for (auto event : events) {
    std::string proto_file_name =
        event.substr(event.find('/') + 1, std::string::npos) + ".proto";
    std::string group = event.substr(0, event.find('/'));
    std::string input_path = input_dir + event + std::string("/format");
    std::string output_path = output_dir + std::string("/") + proto_file_name;

    std::ifstream fin(input_path.c_str(), std::ios::in);
    if (!fin) {
      fprintf(stderr, "Failed to open %s\n", input_path.c_str());
      return 1;
    }
    std::ostringstream stream;
    stream << fin.rdbuf();
    fin.close();
    std::string contents = stream.str();

    perfetto::FtraceEvent format;
    if (!perfetto::ParseFtraceEvent(contents, &format)) {
      fprintf(stderr, "Could not parse file %s.\n", input_path.c_str());
      return 1;
    }

    perfetto::Proto proto;
    if (!perfetto::GenerateProto(format, &proto)) {
      fprintf(stderr, "Could not generate proto for file %s\n",
              input_path.c_str());
      return 1;
    }

    events_info.push_back(perfetto::SingleEventInfo(format, proto, group));

    std::ofstream fout(output_path.c_str(), std::ios::out);
    if (!fout) {
      fprintf(stderr, "Failed to open %s\n", output_path.c_str());
      return 1;
    }

    fout << proto.ToString();
    fout.close();
  }

  perfetto::GenerateEventInfo(events_info);
}
