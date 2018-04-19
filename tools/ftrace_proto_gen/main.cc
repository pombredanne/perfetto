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

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include "perfetto/base/file_utils.h"
#include "perfetto/ftrace_reader/format_parser.h"
#include "perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "tools/ftrace_proto_gen/ftrace_proto_gen.h"

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
  std::vector<std::string> events_info;

  google::protobuf::DescriptorPool descriptor_pool;
  descriptor_pool.AllowUnknownDependencies();
  {
    google::protobuf::FileDescriptorSet file_descriptor_set;
    std::string descriptor_bytes;
    if (!perfetto::base::ReadFile(proto_descriptor, &descriptor_bytes)) {
      fprintf(stderr, "Failed to open %s\n", proto_descriptor.c_str());
      return 1;
    }
    file_descriptor_set.ParseFromString(descriptor_bytes);

    for (const auto& d : file_descriptor_set.file()) {
      PERFETTO_CHECK(descriptor_pool.BuildFile(d));
    }
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
  for (const auto& event : whitelist) {
    if (event == "removed")
      continue;
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

  uint32_t proto_field_id = 2;
  for (auto event : whitelist) {
    ++proto_field_id;
    if (event == "removed") {
      continue;
    }
    std::string name = event.substr(event.find('/') + 1, std::string::npos);
    std::string proto_file_name = name + ".proto";
    std::string output_path = output_dir + std::string("/") + proto_file_name;
    std::string group = event.substr(0, event.find('/'));

    std::string proto_name = perfetto::ToCamelCase(name) + "FtraceEvent";
    perfetto::Proto proto;
    proto.name = proto_name;
    proto.event_name = name;
    const google::protobuf::Descriptor* d =
        descriptor_pool.FindMessageTypeByName("perfetto.protos." + proto_name);
    if (d)
      proto = perfetto::Proto(event, *d);
    else
      PERFETTO_LOG("Did not find %s", proto_name.c_str());
    for (int i = optind; i < argc; ++i) {
      std::string input_dir = argv[i];
      std::string input_path = input_dir + event + std::string("/format");

      std::string contents;
      if (!perfetto::base::ReadFile(input_path, &contents)) {
        fprintf(stderr, "Failed to open %s\n", input_path.c_str());
        continue;
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
