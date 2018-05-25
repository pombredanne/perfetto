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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <string>

#include "perfetto/base/logging.h"

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  if (mkfifo(argv[1], 0666) == -1)
    return 1;
  int fd = open(argv[1], O_RDONLY);
  long rd = -1;
  std::string output;
  do {
    char foo[2048];
    rd = PERFETTO_EINTR(read(fd, &foo, sizeof(foo)));
    PERFETTO_CHECK(rd != -1);
    std::string newdata;
    newdata.assign(foo, static_cast<size_t>(rd));
    output += newdata;
  } while (rd != 0);
  printf("%lu\n", output.size());
  std::ofstream s("stack");
  s << output;
  return 0;
}
