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

#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/io/tokenizer.h"

#include "perfetto/base/file_utils.h"
#include "perfetto/base/logging.h"
#include "perfetto/protozero/message.h"
#include "perfetto/protozero/message_handle.h"
#include "src/protozero/scattered_stream_memory_delegate.h"

namespace perfetto {
constexpr char kConfigDescriptorPath[] =
    "out/r/gen/protos/perfetto/trace/config.descriptor";
constexpr char kConfigProtoName[] = "perfetto.protos.TraceConfig";

using ::google::protobuf::Descriptor;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::DescriptorPool;
using ::google::protobuf::FileDescriptorSet;
using ::google::protobuf::io::ErrorCollector;
using ::google::protobuf::io::Tokenizer;
using ::google::protobuf::io::ZeroCopyInputStream;

namespace {

bool ParsePbTxt(Tokenizer* input,
                const Descriptor* descriptor,
                protozero::Message* output);

class ErrorPrinter : public ErrorCollector {
  virtual void AddError(int line, int col, const std::string& msg) override;

  virtual void AddWarning(int line, int col, const std::string& msg) override;
};

void ErrorPrinter::AddError(int line, int col, const std::string& msg) {
  PERFETTO_ELOG("%d:%d: %s", line, col, msg.c_str());
}

void ErrorPrinter::AddWarning(int line, int col, const std::string& msg) {
  PERFETTO_ILOG("%d:%d: %s", line, col, msg.c_str());
}

bool ParsePbTxt(Tokenizer* input,
                const Descriptor* descriptor,
                protozero::Message* output) {
  PERFETTO_DCHECK(input);
  PERFETTO_DCHECK(descriptor);
  PERFETTO_DCHECK(output);
  while (input->current().type != Tokenizer::TYPE_END) {
    const FieldDescriptor* field = descriptor->FindFieldByName("duration_ms");
    PERFETTO_CHECK(field);
    // output->AppendVarInt<uint64_t>(static_cast<uint32_t>(field->index()),
    // 1234);
    input->Next();
  }
  return true;
}

}  // namespace

std::vector<uint8_t> PbtxtToPb(ZeroCopyInputStream* input) {
  ErrorPrinter errors;
  Tokenizer tokenizer(input, &errors);
  ScatteredStreamMemoryDelegate stream_delegate(base::kPageSize);
  protozero::ScatteredStreamWriter stream(&stream_delegate);

  DescriptorPool descriptor_pool;
  descriptor_pool.AllowUnknownDependencies();

  FileDescriptorSet file_descriptor_set;
  std::string descriptor_bytes;
  if (!base::ReadFile(kConfigDescriptorPath, &descriptor_bytes)) {
    PERFETTO_ELOG("Failed to open %s\n", kConfigDescriptorPath);
    return std::vector<uint8_t>();
  }
  file_descriptor_set.ParseFromString(descriptor_bytes);
  for (const auto& descriptor : file_descriptor_set.file()) {
    PERFETTO_CHECK(descriptor_pool.BuildFile(descriptor));
  }

  const Descriptor* descriptor =
      descriptor_pool.FindMessageTypeByName(kConfigProtoName);
  PERFETTO_CHECK(descriptor);

  protozero::Message message;
  message.Reset(&stream);
  protozero::MessageHandle<protozero::Message> handle(&message);

  PERFETTO_CHECK(ParsePbTxt(&tokenizer, descriptor, &message));

  return stream_delegate.StitchChunks();
}

}  // namespace perfetto
