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

#include <getopt.h>
#include <sys/stat.h>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "perfetto/base/file_utils.h"
#include "perfetto/ftrace_reader/format_parser.h"
#include "perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "tools/ftrace_proto_gen/ftrace_proto_gen.h"

#include "buildtools/protobuf/src/google/protobuf/descriptor.pb.h"

int main(int argc, char** argv) {
  static struct option long_options[] = {
      {"whitelist_path", required_argument, nullptr, 'w'},
      {"output_dir", required_argument, nullptr, 'o'},
      {"proto_descriptor", required_argument, nullptr, 'd'},
  };

  int option_index;
  int c;

  std::string whitelist_path;
  std::string output_dir;
  std::string proto_descriptor;

  while ((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
    switch (c) {
      case 'w':
        whitelist_path = optarg;
        break;
      case 'o':
        output_dir = optarg;
        break;
      case 'd':
        proto_descriptor = optarg;
    }
  }

  PERFETTO_CHECK(!whitelist_path.empty());
  PERFETTO_CHECK(!output_dir.empty());
  PERFETTO_CHECK(!proto_descriptor.empty());

  if (optind >= argc) {
    fprintf(stderr,
            "Usage: ./%s -w whitelist_dir -o output_dir -d proto_descriptor "
            "input_dir...\n",
            argv[0]);
    return 1;
  }

  std::vector<std::string> whitelist = perfetto::GetFileLines(whitelist_path);
  std::set<std::string> events = perfetto::GetWhitelistedEvents(whitelist);
  std::vector<std::string> events_info;

  google::protobuf::FileDescriptorSet file_descriptor_set;
  {
    std::string descriptor_bytes;
    if (!perfetto::base::ReadFile(proto_descriptor, &descriptor_bytes)) {
      fprintf(stderr, "Failed to open %s\n", proto_descriptor.c_str());
      return 1;
    }
    file_descriptor_set.ParseFromString(descriptor_bytes);
  }

  perfetto::GenerateFtraceEventProto(whitelist);

  std::string ftrace;
  if (!perfetto::base::ReadFile(
          "protos/perfetto/trace/ftrace/ftrace_event.proto", &ftrace)) {
    fprintf(stderr, "Failed to open %s\n",
            "protos/perfetto/trace/ftrace/ftrace_event.proto");
    return 1;
  }

  std::set<std::string> new_events;
  for (const auto& event : events) {
    std::string file_name =
        event.substr(event.find('/') + 1, std::string::npos);
    struct stat buf;
    if (stat(("protos/perfetto/trace/ftrace/" + file_name + ".proto").c_str(),
             &buf) == -1) {
      new_events.insert(file_name);
    }
  }

  if (!new_events.empty()) {
    perfetto::PrintEventFormatterMain(new_events);
    perfetto::PrintEventFormatterUsingStatements(new_events);
    perfetto::PrintEventFormatterFunctions(new_events);
    printf(
        "\nAdd output to ParseInode in "
        "tools/ftrace_proto_gen/ftrace_inode_handler.cc\n");
  }

  for (auto event : events) {
    std::string proto_file_name =
        event.substr(event.find('/') + 1, std::string::npos) + ".proto";
    std::string output_path = output_dir + std::string("/") + proto_file_name;
    std::string group = event.substr(0, event.find('/'));

    perfetto::Proto proto;
    for (int i = optind; i < argc; ++i) {
      std::string input_dir = argv[i];
      std::string input_path = input_dir + event + std::string("/format");

      std::string contents;
      if (!perfetto::base::ReadFile(input_path, &contents)) {
        fprintf(stderr, "Failed to open %s\n", input_path.c_str());
        return 1;
      }

      perfetto::FtraceEvent format;
      if (!perfetto::ParseFtraceEvent(contents, &format)) {
        fprintf(stderr, "Could not parse file %s.\n", input_path.c_str());
        return 1;
      }

      perfetto::Proto event_proto;
      if (!perfetto::GenerateProto(format, &event_proto)) {
        fprintf(stderr, "Could not generate proto for file %s\n",
                input_path.c_str());
        return 1;
      }
      proto.MergeFrom(event_proto);
    }

    std::smatch match;
    std::regex event_regex(proto.event_name + "\\s*=\\s*(\\d+)");
    std::regex_search(ftrace, match, event_regex);
    std::string proto_field_id = match[1].str().c_str();
    if (proto_field_id == "") {
      fprintf(stderr,
              "Could not find proto_field_id for %s in ftrace_event.proto. "
              "Please add it.\n",
              proto.name.c_str());
      return 1;
    }

    if (!new_events.empty())
      PrintInodeHandlerMain(proto.name, proto);

    events_info.push_back(
        perfetto::SingleEventInfo(proto, group, proto_field_id));

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
