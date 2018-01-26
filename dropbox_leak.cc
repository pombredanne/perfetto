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
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

#include "perfetto/base/android_task_runner.h"

#include <android/os/DropBoxManager.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>


int main() {
  ::perfetto::base::AndroidTaskRunner task_runner;

  printf("Hello, PID=%d\n", getpid());
  for (int i = 0; i < 100; i++) {
    char path[255];
    sprintf(path, "/data/local/tmp/tmp.%d", i);
    printf("Trying path %s", path);
    int fd = open(path, O_RDWR | O_CLOEXEC | O_CREAT, 0666);
    PERFETTO_CHECK(fd >= 0);
    write(fd, "foo\n", 4);
    close(fd);

    android::sp<android::os::DropBoxManager> dropbox =
        new android::os::DropBoxManager();
    android::binder::Status status = dropbox->addFile(
        android::String16("leaktest"), path, 0 /* flags */);
    unlink(path);
    printf("Binder result %d: %d\n", i, status.isOk());
  }
  printf("Done, now look at /proc/%d/fd")
  task_runner.Run();
}
