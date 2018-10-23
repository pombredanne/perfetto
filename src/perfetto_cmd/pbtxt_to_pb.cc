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

#include "src/perfetto_cmd/pbtxt_to_pb.h"

#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "src/perfetto_cmd/descriptor.pb.h"

#include "perfetto/base/file_utils.h"
#include "perfetto/base/logging.h"
#include "perfetto/protozero/message.h"
#include "perfetto/protozero/message_handle.h"
#include "src/protozero/scattered_stream_memory_delegate.h"
#include "src/perfetto_cmd/trace_config_descriptor.h"

namespace perfetto {
constexpr char kConfigProtoName[] = ".perfetto.protos.TraceConfig";

using protos::DescriptorProto;
using protos::FieldDescriptorProto;
using protos::FileDescriptorSet;
using ::google::protobuf::io::ZeroCopyInputStream;
using ::google::protobuf::io::ArrayInputStream;

namespace {

const char* kWhitespace = " \n\t\x0b\x0c\r";
const char* kNumeric = "0123456789";
const char* kIdentifierBody =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
// const char* kIdentifierHeader =
// "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";

constexpr bool IsIdentifier(char c) {
  return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '_';
}

constexpr bool IsNumeric(char c) {
  return '0' <= c && c <= '9';
}

constexpr bool IsSymbol(char c) {
  return c == ':' || c == ';' || c == '{' || c == '}';
}

bool IsWhitespace(char c) {
  for (size_t i = 0; i < strlen(kWhitespace); i++) {
    if (c == kWhitespace[i])
      return true;
  }
  return false;
}

enum TokenType {
  kTypeStart,
  kTypeEnd,
  kTypeIdentifier,
  kTypeInteger,
  kTypeFloat,
  kTypeString,
  kTypeSymbol
};

struct Token {
  TokenType type;
  std::string text;

  static Token Start() { return Token{kTypeStart, ""}; }

  static Token End() { return Token{kTypeEnd, ""}; }

  friend bool operator==(const Token& x, const Token& y) {
    return std::tie(x.type, x.text) == std::tie(y.type, y.text);
  }

  friend bool operator!=(const Token& x, const Token& y) { return !(x == y); }
};

class Tokenizer {
 public:
  Tokenizer(const std::string& input, ErrorReporter* reporter)
      : input_(input), reporter_(reporter) {}

  void Next() {
    if (current_token_.type == kTypeEnd)
      return;

    ConsumeWhitespace();

    if (i_ == input_.size()) {
      current_token_ = Token::End();
      return;
    }

    const char c = input_[i_];
    if (IsIdentifier(c)) {
      return NextIdentifier();
    } else if (IsNumeric(c)) {
      return NextNumber();
    } else if (IsSymbol(c)) {
      return NextSymbol();
    }
    char buf[50];
    sprintf(buf, "Unexpected character \"%c\"", c);
    reporter_->AddError(0, 0, 0, buf);
    current_token_ = Token::End();
  }

  const Token& current() { return current_token_; }

 private:
  void ConsumeWhitespace() {
    if (i_ == input_.size() || !IsWhitespace(input_[i_]))
      return;

    for (; i_ < input_.size(); i_++) {
      column_++;
      if (input_[i_] == '\n') {
        column_ = 0;
        row_++;
      }
      if (!IsWhitespace(input_[i_]))
        break;
    }
  }

  void NextIdentifier() {
    auto start = i_;
    i_ = input_.find_first_not_of(kIdentifierBody, i_);
    current_token_ =
        Token{kTypeIdentifier, std::string(input_, start, i_ - start)};
  }

  void NextNumber() {
    auto start = i_;
    i_ = input_.find_first_not_of(kNumeric, i_);
    current_token_ =
        Token{kTypeInteger, std::string(input_, start, i_ - start)};
  }

  void NextSymbol() {
    current_token_ = Token{kTypeSymbol, std::string(input_, i_, 1)};
    i_++;
  }

  std::string input_;
  ErrorReporter* reporter_;
  size_t i_ = 0;
  size_t row_ = 0;
  size_t column_ = 0;
  Token current_token_ = Token::Start();
};

bool ExpectNumber(Tokenizer* input, uint32_t* number_ptr);
bool ExpectNumber(Tokenizer* input, uint32_t* number_ptr) {
  if (input->current().type != kTypeInteger)
    return false;
  uint32_t n = 0;
  PERFETTO_CHECK(sscanf(input->current().text.c_str(), "%" PRIu32, &n) == 1);
  *number_ptr = n;
  input->Next();
  return true;
}

class Parser {
 public:
  Parser(Tokenizer* tokenizer,
         protozero::Message* message,
         ErrorReporter* reporter,
         std::map<std::string, const DescriptorProto*> name_to_descriptor)
      : input_(tokenizer),
        output_(message),
        reporter_(reporter),
        name_to_descriptor_(std::move(name_to_descriptor)) {}

  bool ParseMessage(const DescriptorProto* descriptor) {
    PERFETTO_CHECK(input_);
    PERFETTO_CHECK(descriptor);
    PERFETTO_CHECK(output_);

    // Move past start.
    input_->Next();

    while (input_->current() != Token::End()) {
      if (!ParseField(descriptor, output_))
        return false;
    }
    return true;
  }

 private:
  bool ParseNestedMessage(const DescriptorProto* descriptor,
                          protozero::Message* msg) {
    while (current().type != kTypeSymbol || current().text != "}") {
      if (!ParseField(descriptor, msg))
        return false;
    }
    input_->Next();
    return true;
  }

  bool ParseField(const DescriptorProto* descriptor, protozero::Message* msg) {
    PERFETTO_ILOG("%d %s", input_->current().type,
                  input_->current().text.c_str());
    std::string name;
    if (!ExpectIdentifier(input_, name)) {
      return false;
    }
    const FieldDescriptorProto* field = nullptr;
    for (const auto& f : descriptor->field()) {
      if (f.name() == name) {
        field = &f;
        break;
      }
    }
    if (field == nullptr) {
      char buf[4096];
      sprintf(buf, "No field with name \"%s\" in proto %s.", name.c_str(),
              descriptor->name().c_str());
      reporter_->AddError(0, 0, 0, buf);
      return false;
    }
    const uint32_t field_id = static_cast<uint32_t>(field->number());
    const auto& field_type = field->type();
    switch (field_type) {
      case FieldDescriptorProto::TYPE_UINT32: {
        uint32_t num = 0;
        PERFETTO_CHECK(TryConsume(':'));
        PERFETTO_ILOG("%s", current().text.c_str());
        PERFETTO_CHECK(ExpectNumber(input_, &num));
        msg->AppendVarInt<uint32_t>(field_id, num);
        PERFETTO_ILOG("%s %u", name.c_str(), num);
        break;
      }
      case FieldDescriptorProto::TYPE_BOOL: {
        PERFETTO_CHECK(TryConsume(':'));
        if (current().type != kTypeIdentifier ||
            (current().text != "true" && current().text != "false")) {
          reporter_->AddError(
              0, 0, 0,
              "Expected 'true' or 'false' instead saw: " + current().text);
          return false;
        }
        msg->AppendTinyVarInt(field_id, current().text == "true" ? 1 : 0);
        input_->Next();
        break;
      }
      case FieldDescriptorProto::TYPE_MESSAGE: {
        PERFETTO_CHECK(TryConsume(':'));
        PERFETTO_CHECK(TryConsume('{'));
        const std::string& type_name = field->type_name();
        PERFETTO_ILOG("%s", type_name.c_str());
        const DescriptorProto* nested_descriptor =
            name_to_descriptor_[type_name];
        PERFETTO_CHECK(nested_descriptor);
        auto* nested_msg =
            msg->BeginNestedMessage<protozero::Message>(field_id);
        ParseNestedMessage(nested_descriptor, nested_msg);
        nested_msg->Finalize();
        break;
      }
      case FieldDescriptorProto::TYPE_DOUBLE:
      case FieldDescriptorProto::TYPE_FLOAT:
      case FieldDescriptorProto::TYPE_INT64:
      case FieldDescriptorProto::TYPE_UINT64:
      case FieldDescriptorProto::TYPE_INT32:
      case FieldDescriptorProto::TYPE_FIXED64:
      case FieldDescriptorProto::TYPE_FIXED32:
      case FieldDescriptorProto::TYPE_STRING:
      case FieldDescriptorProto::TYPE_GROUP:
      case FieldDescriptorProto::TYPE_BYTES:
      case FieldDescriptorProto::TYPE_ENUM:
      case FieldDescriptorProto::TYPE_SFIXED32:
      case FieldDescriptorProto::TYPE_SFIXED64:
      case FieldDescriptorProto::TYPE_SINT32:
      case FieldDescriptorProto::TYPE_SINT64:
        PERFETTO_FATAL("Unhandled type %d", field_type);
    }

    // Fields may be semicolon separated.
    TryConsume(';');
    return true;
  }

  bool TryConsume(char c) {
    if (current().type != kTypeSymbol)
      return false;
    if (current().text.size() != 1 || current().text[0] != c)
      return false;
    input_->Next();
    return true;
  }

  bool ExpectIdentifier(Tokenizer* input, std::string& out) {
    if (input->current().type != kTypeIdentifier)
      return false;
    out = input->current().text;
    input->Next();
    return true;
  }

  Token current() { return input_->current(); }

  Tokenizer* input_;
  protozero::Message* output_;
  ErrorReporter* reporter_;
  std::map<std::string, const DescriptorProto*> name_to_descriptor_;
};

void AddNestedDescriptors(
    const std::string& prefix,
    const DescriptorProto* descriptor,
    std::map<std::string, const DescriptorProto*>* name_to_descriptor) {
  for (const DescriptorProto& nested_descriptor : descriptor->nested_type()) {
    const std::string name = prefix + "." + nested_descriptor.name();
    (*name_to_descriptor)[name] = &nested_descriptor;
    AddNestedDescriptors(name, &nested_descriptor, name_to_descriptor);
  }
}

}  // namespace

ErrorReporter::ErrorReporter() = default;
ErrorReporter::~ErrorReporter() = default;

std::vector<uint8_t> PbtxtToPb(const std::string& input,
                               ErrorReporter* reporter) {
  Tokenizer tokenizer(input, reporter);
  ScatteredStreamMemoryDelegate stream_delegate(base::kPageSize);
  protozero::ScatteredStreamWriter stream(&stream_delegate);
  stream_delegate.set_writer(&stream);

  std::map<std::string, const DescriptorProto*> name_to_descriptor;
  FileDescriptorSet file_descriptor_set;
  {
    ArrayInputStream config_descriptor_stream(kTraceConfigDescriptor, static_cast<int>(kTraceConfigDescriptorSize));
    file_descriptor_set.ParseFromZeroCopyStream(&config_descriptor_stream);
    for (const auto& file_descriptor : file_descriptor_set.file()) {
      for (const auto& descriptor : file_descriptor.message_type()) {
        const std::string name =
            "." + file_descriptor.package() + "." + descriptor.name();
        name_to_descriptor[name] = &descriptor;
        AddNestedDescriptors(name, &descriptor, &name_to_descriptor);
      }
    }
  }
  const DescriptorProto* descriptor = name_to_descriptor[kConfigProtoName];
  PERFETTO_CHECK(descriptor);

  {
    protozero::Message message;
    message.Reset(&stream);
    Parser(&tokenizer, &message, reporter, std::move(name_to_descriptor))
        .ParseMessage(descriptor);
    //    PERFETTO_CHECK(ParseMessage(&tokenizer, descriptor, &message));
  }

  return stream_delegate.StitchChunks();
}

}  // namespace perfetto
