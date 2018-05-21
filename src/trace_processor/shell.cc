#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/trace_processor/blob_reader.h"
#include "perfetto/trace_processor/db.h"

namespace {

perfetto::trace_processor::DB* g_db;

class BlobReaderImpl : public perfetto::trace_processor::BlobReader {
 public:
  explicit BlobReaderImpl(const char* path) {
    f_.reset(open(path, O_RDONLY));
    if (!f_) {
      PERFETTO_PLOG("Cannot open %s", path);
      exit(1);
    }
  }

  uint32_t Read(uint32_t offset, uint32_t len, uint8_t* dst) override {
    auto rsize = pread(*f_, dst, len, offset);
    if (rsize < 0)
      rsize = 0;
    return static_cast<uint32_t>(rsize);
  }

 private:
  perfetto::base::ScopedFile f_;
};

void ShowPrompt() {
  printf("%80s\r> ", "");
  fflush(stdout);
}

void OnStdin() {
  char buf[1024];
  auto rsize = read(STDIN_FILENO, buf, sizeof(buf));
  if (rsize <= 0)
    exit(0);
  std::string sql(buf, static_cast<size_t>(rsize));
  g_db->Query(sql.c_str());
  ShowPrompt();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("Usage: %s path_to_perfetto_trace.proto\n", argv[0]);
    return 1;
  }
  perfetto::base::UnixTaskRunner task_runner;
  perfetto::trace_processor::DB db(&task_runner);
  g_db = &db;
  BlobReaderImpl reader(argv[1]);
  db.LoadTrace(&reader);
  ShowPrompt();
  task_runner.AddFileDescriptorWatch(STDIN_FILENO, OnStdin);
  task_runner.Run();
}
