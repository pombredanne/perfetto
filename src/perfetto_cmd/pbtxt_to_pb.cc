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

#include <stack>

#include "src/perfetto_cmd/pbtxt_to_pb.h"

#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "src/perfetto_cmd/descriptor.pb.h"

#include "perfetto/base/file_utils.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/string_view.h"
#include "perfetto/protozero/message.h"
#include "perfetto/protozero/message_handle.h"
#include "src/protozero/scattered_stream_memory_delegate.h"
#include "src/perfetto_cmd/trace_config_descriptor.h"

namespace perfetto {
constexpr char kConfigProtoName[] = ".perfetto.protos.TraceConfig";

using protos::DescriptorProto;
using protos::EnumDescriptorProto;
using protos::EnumValueDescriptorProto;
using protos::FieldDescriptorProto;
using protos::FileDescriptorSet;
using ::google::protobuf::io::ZeroCopyInputStream;
using ::google::protobuf::io::ArrayInputStream;

namespace {

const char* kWhitespace = " \n\t\x0b\x0c\r";

constexpr bool IsNumeric(char c) {
  return '0' <= c && c <= '9';
}

constexpr bool IsIdentifierStart(char c) {
  return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '_';
}

constexpr bool IsIdentifierBody(char c) {
  return IsIdentifierStart(c) || IsNumeric(c);
}

bool IsWhitespace(char c) {
  for (size_t i = 0; i < strlen(kWhitespace); i++) {
    if (c == kWhitespace[i])
      return true;
  }
  return false;
}

std::string Format(const char* fmt, std::map<std::string, std::string> args) {
  std::string result(fmt);
  for (const auto& key_value : args) {
    PERFETTO_CHECK(result.find(key_value.first) != std::string::npos);
    size_t start = result.find(key_value.first);
    result.replace(start, key_value.first.size(), key_value.second);
    PERFETTO_CHECK(result.find(key_value.first) == std::string::npos);
  }
  return result;
}

enum ParseState {
  kWaitingForKey,
  kReadingKey,
  kWaitingForValue,
  kReadingStringValue,
  kReadingNumericValue,
  kReadingIdentifierValue,
};

struct Token {
  size_t offset;
  size_t column;
  size_t row;
  base::StringView txt;

  size_t size() const { return txt.size(); }
  std::string ToStdString() const { return txt.ToStdString(); }
};

struct ParserDelegateContext {
  const DescriptorProto* descriptor;
  protozero::Message* message;
};

class ParserDelegate {
 public:
  ParserDelegate(
        const DescriptorProto* descriptor,
        protozero::Message* message,
         ErrorReporter* reporter,
         std::map<std::string, const DescriptorProto*> name_to_descriptor,
         std::map<std::string, const EnumDescriptorProto*> name_to_enum) :
       reporter_(reporter),
        name_to_descriptor_(std::move(name_to_descriptor)),
        name_to_enum_(std::move(name_to_enum)) {
     ctx_.push(ParserDelegateContext{descriptor, message});
  }

  void NumericField(Token key, Token value) {
    const FieldDescriptorProto* field = FindFieldByName(key.ToStdString());
    if (!field)
      return;
    uint32_t field_id = static_cast<uint32_t>(field->number());
    //const auto& field_type = field->type();
    uint64_t n = 0;
    PERFETTO_CHECK(ParseInteger(value.txt, &n));
    msg()->AppendVarInt<uint64_t>(field_id, n);
  }

  void StringField(Token key, Token value) {
    const FieldDescriptorProto* field = FindFieldByName(key.ToStdString());
    if (!field)
      return;
    uint32_t field_id = static_cast<uint32_t>(field->number());
    const auto& field_type = field->type();
    PERFETTO_CHECK(field_type == FieldDescriptorProto::TYPE_STRING || field_type == FieldDescriptorProto::TYPE_BYTES);

    msg()->AppendBytes(field_id, value.txt.data(), value.size());
  }

  void IdentifierField(Token key, Token value) {
    const FieldDescriptorProto* field = FindFieldByName(key.ToStdString());
    if (!field)
      return;
    uint32_t field_id = static_cast<uint32_t>(field->number());
    const auto& field_type = field->type();
    if (field_type == FieldDescriptorProto::TYPE_BOOL) {
      if (value.txt != "true" && value.txt != "false") {
        AddError(value,
            "Expected either 'true' or 'false' for boolean field $k in "
            "proto $n instead saw '$v'", std::map<std::string, std::string>{
              {"$k", key.ToStdString()},
              {"$n", descriptor_name()},
              {"$v", value.ToStdString()},
            });
        return;
      }
      msg()->AppendTinyVarInt(field_id, value.txt == "true" ? 1 : 0);
    } else if (field_type == FieldDescriptorProto::TYPE_ENUM) {
        const std::string& type_name = field->type_name();
        const EnumDescriptorProto* enum_descriptor = name_to_enum_[type_name];
        PERFETTO_CHECK(enum_descriptor);
        bool found_value = false;
        int32_t enum_value_number = 0;
        for (const EnumValueDescriptorProto& enum_value : enum_descriptor->value()) {
          if (value.ToStdString() != enum_value.name())
            continue;
          found_value = true;
          enum_value_number = enum_value.number();
          break;
        }
        PERFETTO_CHECK(found_value);
        msg()->AppendVarInt<int32_t>(field_id, enum_value_number);
    }

  }

  void BeginNestedMessage(Token key) {
    const FieldDescriptorProto* field = FindFieldByName(key.ToStdString());
    if (!field)
      return;

    uint32_t field_id = static_cast<uint32_t>(field->number());
    //const auto& field_type = field->type();

    const std::string& type_name = field->type_name();
    const DescriptorProto* nested_descriptor =
        name_to_descriptor_[type_name];
    PERFETTO_CHECK(nested_descriptor);

    auto* nested_msg = msg()->BeginNestedMessage<protozero::Message>(field_id);
    ctx_.push(ParserDelegateContext{nested_descriptor, nested_msg});
  }

  void EndNestedMessage() {
    msg()->Finalize();
    ctx_.pop();
  }

  void Eof() {
  }

 private:

  template<typename T>
  bool ParseInteger(base::StringView s, T* number_ptr) {
    uint64_t n = 0;
    PERFETTO_CHECK(sscanf(s.ToStdString().c_str(), "%" PRIu64, &n) == 1);
    PERFETTO_CHECK(n <= std::numeric_limits<T>::max());
    *number_ptr = static_cast<T>(n);
    return true;
  }

  const FieldDescriptorProto* FindFieldByName(const std::string& name) {
    for (const auto& f : descriptor()->field()) {
      if (f.name() == name)
        return &f;
    }
    reporter_->AddError(0,0,0, "No field in");
    return nullptr;
  }

  const DescriptorProto* descriptor() {
    PERFETTO_CHECK(!ctx_.empty());
    return ctx_.top().descriptor;
  }

  const std::string& descriptor_name() {
    return descriptor()->name();
  }

  protozero::Message* msg() {
    PERFETTO_CHECK(!ctx_.empty());
    return ctx_.top().message;
  }

  void AddError(Token token, const char* fmt, const std::map<std::string, std::string>& args) {
    reporter_->AddError(token.row, token.column, token.size(), Format(fmt, args));
  }

  std::stack<ParserDelegateContext> ctx_;
  ErrorReporter* reporter_;
  std::map<std::string, const DescriptorProto*> name_to_descriptor_;
  std::map<std::string, const EnumDescriptorProto*> name_to_enum_;
};

void Parse(const std::string& input, ParserDelegate* delegate) {
  ParseState state = kWaitingForKey;
  size_t column = 0;
  size_t row = 0;
  size_t depth = 0;
  bool saw_colon_for_this_key = false;
  bool saw_semicolon_for_this_value = true;
  bool comment_till_eol = false;
  Token key;
  Token value;

  for (size_t i = 0; i < input.size(); i++, column++) {
    bool last_character = i + 1 == input.size();
    char c = input.at(i);
    if (c == '\n') {
      column = 0;
      row++;
      if (comment_till_eol) {
        comment_till_eol = false;
        continue;
      }
    }
    if (comment_till_eol)
      continue;

    switch (state) {
      case kWaitingForKey:
        if (IsWhitespace(c))
          continue;
        if (c == '#') {
          comment_till_eol = true;
          continue;
        }
        if (depth > 0 && c == '}') {
          saw_semicolon_for_this_value = false;
          depth--;
          delegate->EndNestedMessage();
          continue;
        }
        if (!saw_semicolon_for_this_value && c == ';') {
          saw_semicolon_for_this_value = true;
          continue;
        }
        if (IsIdentifierStart(c)) {
          saw_colon_for_this_key = false;
          state = kReadingKey;
          key.offset = i;
          key.row = row;
          key.column = column;
          continue;
        }
        break;

      case kReadingKey:
        if (IsIdentifierBody(c))
          continue;
        key.txt = base::StringView(input.data() + key.offset, i-key.offset);
        PERFETTO_ILOG("%s", key.ToStdString().c_str());
        state = kWaitingForValue;
        if (c == '#')
          comment_till_eol = true;
        continue;

      case kWaitingForValue:
        if (IsWhitespace(c))
          continue;
        if (c == '#') {
          comment_till_eol = true;
          continue;
        }
        value.offset = i;
        value.row = row;
        value.column = column;

        if (c == ':' && !saw_colon_for_this_key) {
          saw_colon_for_this_key = true;
          continue;
        }
        if (c == '"') {
          state = kReadingStringValue;
          continue;
        }
        if (c == '-' || IsNumeric(c)) {
          state = kReadingNumericValue;
          continue;
        }
        if (IsIdentifierStart(c)) {
          state = kReadingIdentifierValue;
          continue;
        }
        if (c == '{') {
          state = kWaitingForKey;
          depth++;
          delegate->BeginNestedMessage(key);
          continue;
        }
        break;

      case kReadingNumericValue:
        if (IsWhitespace(c) || c == ';' || last_character) {
          size_t size = i-value.offset + (last_character ? 1 : 0);
          value.txt = base::StringView(input.data() + value.offset, size);
          saw_semicolon_for_this_value = c == ';';
          state = kWaitingForKey;
          delegate->NumericField(key, value);
          continue;
        }
        if (IsNumeric(c))
          continue;
        break;

      case kReadingStringValue:
        if (c == '"') {
          size_t size = i - value.offset - 1;
          value.txt = base::StringView(input.data() + value.offset, size);
          saw_semicolon_for_this_value = false;
          state = kWaitingForKey;
          delegate->StringField(key, value);
          continue;
        }
        continue;

      case kReadingIdentifierValue:
        if (IsWhitespace(c) || c == ';' || c == '#' || last_character) {
          size_t size = i-value.offset + (last_character ? 1 : 0);
          value.txt = base::StringView(input.data() + value.offset, size);
          comment_till_eol = c == '#';
          saw_semicolon_for_this_value = c == ';';
          state = kWaitingForKey;
          delegate->IdentifierField(key, value);
          continue;
        }
        if (IsIdentifierBody(c)) {
          continue;
        }
        break;
    }
    PERFETTO_FATAL("Unexpected char %c", c);
  } // for
  delegate->Eof();
}

void AddNestedDescriptors(
    const std::string& prefix,
    const DescriptorProto* descriptor,
    std::map<std::string, const DescriptorProto*>* name_to_descriptor,
    std::map<std::string, const EnumDescriptorProto*>* name_to_enum) {
  for (const EnumDescriptorProto& enum_descriptor : descriptor->enum_type()) {
    const std::string name = prefix + "." + enum_descriptor.name();
    (*name_to_enum)[name] = &enum_descriptor;
  }
  for (const DescriptorProto& nested_descriptor : descriptor->nested_type()) {
    const std::string name = prefix + "." + nested_descriptor.name();
    (*name_to_descriptor)[name] = &nested_descriptor;
    AddNestedDescriptors(name, &nested_descriptor, name_to_descriptor, name_to_enum);
  }
}

}  // namespace

ErrorReporter::ErrorReporter() = default;
ErrorReporter::~ErrorReporter() = default;

std::vector<uint8_t> PbtxtToPb(const std::string& input,
                               ErrorReporter* reporter) {
    std::map<std::string, const DescriptorProto*> name_to_descriptor;
  std::map<std::string, const EnumDescriptorProto*> name_to_enum;
  FileDescriptorSet file_descriptor_set;
  {
    ArrayInputStream config_descriptor_stream(kTraceConfigDescriptor, static_cast<int>(kTraceConfigDescriptorSize));
    file_descriptor_set.ParseFromZeroCopyStream(&config_descriptor_stream);
    for (const auto& file_descriptor : file_descriptor_set.file()) {
      for (const auto& enum_descriptor : file_descriptor.enum_type()) {
        const std::string name =
            "." + file_descriptor.package() + "." + enum_descriptor.name();
        name_to_enum[name] = &enum_descriptor;
      }
      for (const auto& descriptor : file_descriptor.message_type()) {
        const std::string name =
            "." + file_descriptor.package() + "." + descriptor.name();
        name_to_descriptor[name] = &descriptor;
        AddNestedDescriptors(name, &descriptor, &name_to_descriptor, &name_to_enum);
      }
    }
  }
  const DescriptorProto* descriptor = name_to_descriptor[kConfigProtoName];
  PERFETTO_CHECK(descriptor);

  ScatteredStreamMemoryDelegate stream_delegate(base::kPageSize);
  protozero::ScatteredStreamWriter stream(&stream_delegate);
  stream_delegate.set_writer(&stream);

  protozero::Message message;
  message.Reset(&stream);
  ParserDelegate delegate(
    descriptor,
    &message,
    reporter,
    std::move(name_to_descriptor),
    std::move(name_to_enum));
  Parse(input, &delegate);
  return stream_delegate.StitchChunks();
}

}  // namespace perfetto
