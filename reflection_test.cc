
#include <stdio.h>

#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/util/field_comparator.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/util/message_differencer.h>

using namespace google::protobuf;
using namespace google::protobuf::compiler;

class MFE : public MultiFileErrorCollector {
  virtual void AddError(const string& filename, int line, int column,
                        const string& message) {
                          printf("Error %s %d:%d: %s", filename.c_str(), line, column, message.c_str());
                        }

  virtual void AddWarning(const string& filename, int line, int column,
                          const string& message) {
                          printf("Error %s %d:%d: %s", filename.c_str(), line, column, message.c_str());

                          }

};

int main() {
  DiskSourceTree dst;
  dst.MapPath("protos", "protos");

  MFE mfe;
  Importer importer(&dst, &mfe);

//  importer.AddUnusedImportTrackFile(file_name);
  const FileDescriptor* parsed_file = importer.Import("protos/trace_packet.proto");


  printf("descriptor %p %s\n", reinterpret_cast<const void*>(parsed_file), parsed_file->message_type(0)->name().c_str());

  DynamicMessageFactory dmf;
  const Message* msg_root = dmf.GetPrototype(parsed_file->message_type(0));

  Message* msg = msg_root->New();
  char bin[] = {0x12, 0x04, 0x66, 0x6f, 0x6f, 0x6f};
  printf("parsed: %d\n", msg->ParseFromArray(bin, sizeof(bin)));

  Message* msg2 = msg_root->New();
  char bin2[] = {0x12, 0x04, 0x66, 0x6f, 0x6f, 0x6e};
  printf("parsed: %d\n", msg2->ParseFromArray(bin2, sizeof(bin2)));

  util::MessageDifferencer mdiff;
  std::string report;
  mdiff.ReportDifferencesToString(&report);
  printf("equal? %d\n", mdiff.Compare(*msg, *msg2));
  printf("diff: %s\n", report.c_str());

}
