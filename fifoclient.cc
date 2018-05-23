#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "perfetto/base/file_utils.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/string_splitter.h"

static int tbfd;

namespace perfetto {
std::pair<void*, void*> FindStack();
std::pair<void*, void*> FindStack() {
  std::string maps;
  PERFETTO_CHECK(base::ReadFile("/proc/self/maps", &maps));
  std::string pointers;
  std::string name;
  for (base::StringSplitter ss(std::move(maps), '\n'); ss.Next();) {
    const char* line = ss.cur_token();
    int i = 0;
    for (base::StringSplitter ls(line, ' '); ls.Next();) {
      if (i == 0)
        pointers = ls.cur_token();
      if (i == 5)
        name = ls.cur_token();
      ++i;
    }
    if (name == "[stack]")
      break;
  }

  PERFETTO_CHECK(name == "[stack]");
  char* p;
  void* start = reinterpret_cast<void*>(strtoll(pointers.c_str(), &p, 16));
  PERFETTO_CHECK(*p == '-');
  char* p2;
  void* end = reinterpret_cast<void*>(strtoll(p + 1, &p2, 16));
  return {start, end};
}
}  // namespace perfetto

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  tbfd = open(argv[1], O_WRONLY);
  auto stackbounds = perfetto::FindStack();
  printf("%p - %p\n", stackbounds.first, stackbounds.second);
  return 0;
}
