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

#include "ftrace_proto_gen.h"
#include "perfetto/base/logging.h"

#include <fstream>
#include <regex>
#include <set>
#include <string>

namespace perfetto {

namespace {

std::string ToCamelCase(const std::string& s) {
  std::string result;
  result.reserve(s.size());
  bool upperCaseNextChar = true;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (c == '_') {
      upperCaseNextChar = true;
      continue;
    }
    if (upperCaseNextChar) {
      upperCaseNextChar = false;
      c = static_cast<char>(toupper(c));
    }
    result.push_back(c);
  }
  return result;
}

bool StartsWith(const std::string& str, const std::string& prefix) {
  return str.compare(0, prefix.length(), prefix) == 0;
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

std::string InferProtoType(const FtraceEvent::Field& field) {
  // Fixed length strings: "char foo[16]"
  if (std::regex_match(field.type_and_name, std::regex(R"(char \w+\[\d+\])")))
    return "string";

  // String pointers: "__data_loc char[] foo" (as in
  // 'cpufreq_interactive_boost').
  if (Contains(field.type_and_name, "char[] "))
    return "string";
  if (Contains(field.type_and_name, "char * "))
    return "string";

  // Variable length strings: "char* foo"
  if (StartsWith(field.type_and_name, "char *"))
    return "string";

  // Variable length strings: "char foo" + size: 0 (as in 'print').
  if (StartsWith(field.type_and_name, "char ") && field.size == 0)
    return "string";

  // Ints of various sizes:
  if (field.size <= 4 && field.is_signed)
    return "int32";
  if (field.size <= 4 && !field.is_signed)
    return "uint32";
  if (field.size <= 8 && field.is_signed)
    return "int64";
  if (field.size <= 8 && !field.is_signed)
    return "uint64";
  return "";
}

bool GenerateProto(const FtraceEvent& format, Proto* proto_out) {
  proto_out->name = ToCamelCase(format.name) + "FtraceEvent";
  proto_out->fields.reserve(format.fields.size());
  std::set<std::string> seen;
  // TODO(hjd): We should be cleverer about id assignment.
  uint32_t i = 1;
  for (const FtraceEvent::Field& field : format.fields) {
    std::string name = GetNameFromTypeAndName(field.type_and_name);
    // TODO(hjd): Handle dup names.
    if (name == "" || seen.count(name))
      continue;
    seen.insert(name);
    std::string type = InferProtoType(field);
    // Check we managed to infer a type.
    if (type == "")
      continue;
    proto_out->fields.emplace_back(Proto::Field{type, name, i});
    i++;
  }

  return true;
}

std::set<std::string> GetWhitelistedEvents(std::string whitelistPath) {
  std::string line;
  std::set<std::string> whitelist;

  std::ifstream fin(whitelistPath, std::ios::in);
  if (!fin) {
    fprintf(stderr, "Failed to open whitelist %s\n", whitelistPath.c_str());
    return whitelist;
  }
  while (std::getline(fin, line)) {
    if (!StartsWith(line, "#")) {
      whitelist.insert(line);
    }
  }
  return whitelist;
}

// Generates section of event_info.cc for a single event.
std::string SingleEventInfo(perfetto::FtraceEvent format,
                            perfetto::Proto proto,
                            std::string group) {
  std::string s =
      "event->name = \"" + format.name + "\";\nevent->group = \"" + group +
      "\";\nevent->proto_field_id = " + std::to_string(format.id) + ";\n";
  for (auto field : proto.fields) {
    s += "event->fields.push_back(FieldFromNameIdType(\"" + field.name +
         "\", " + std::to_string(field.number) + ", kProto" +
         ToCamelCase(field.type) + "));\n";
  }
  return s;
}

// This will generate the event_info.cc file for the whitelisted protos.
void GenerateEventInfo(std::vector<std::string> events_info) {
  std::string output_path = "src/ftrace_reader/event_info.cc";
  std::ofstream fout(output_path.c_str(), std::ios::out);
  if (!fout) {
    fprintf(stderr, "Failed to open %s\n", output_path.c_str());
  }

  std::string s = std::string("// Autogenerated by ") + __FILE__ +
                  " do not edit.\n\n#include "
                  "\"src/ftrace_reader/event_info.h\"\n\nnamespace perfetto "
                  "{\n\nstd::vector<Event> GetStaticEventInfo() "
                  "{\nstd::vector<Event> events;\n";
  for (auto event : events_info) {
    s +=
        "\n{\nevents.emplace_back(Event{});\nEvent* event = &events.back();\n" +
        event + "}\n";
  }
  s += "\n  return events;\n}\n\n}  // namespace perfetto\n";

  fout << s;
  fout.close();
}

std::string Proto::ToString() {
  std::string s = "// Autogenerated by " __FILE__
                  " do not edit.\n"
                  "syntax = \"proto2\";\n"
                  "option optimize_for = LITE_RUNTIME;\n"
                  "package perfetto.protos;\n"
                  "\n";
  s += "message " + name + " {\n";
  for (const Proto::Field& field : fields) {
    s += "  optional " + field.type + " " + field.name + " = " +
         std::to_string(field.number) + ";\n";
  }
  s += "}\n";
  return s;
}

}  // namespace perfetto
