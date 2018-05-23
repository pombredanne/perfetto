#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  int fd = mkfifo(argv[1], 0666);
  char foo[2048];
  read(fd, &foo, sizeof(foo));
  return 0;
}
