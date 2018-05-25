#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

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

std::pair<void*, void*> GetStack();
std::pair<void*, void*> GetStack() {
  static std::pair<void*, void*> stack = FindStack();
  return stack;
}

void SendStack();
void SendStack() {
  auto stackbounds = perfetto::GetStack();
  void* sp = __builtin_frame_address(0);
  size_t size = reinterpret_cast<uintptr_t>(stackbounds.second) -
                reinterpret_cast<uintptr_t>(sp);
  // ~7-15 ticks.
  struct iovec v[1];
  v[0].iov_base = sp;
  v[0].iov_len = size;
  PERFETTO_CHECK(vmsplice(tbfd, v, 1, 0) != -1);
  // ~30-40 ticks.
  // PERFETTO_CHECK(write(tbfd, sp, size) == static_cast<ssize_t>(size));
}

}  // namespace perfetto

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;

  // Warm stack cache.
  perfetto::GetStack();
  tbfd = open(argv[1], O_WRONLY);
  clock_t t = clock();
  perfetto::SendStack();
  t = clock() - t;
  printf("It took me %ld clicks (%ld per s).\n", t, CLOCKS_PER_SEC);
  return 0;
}
