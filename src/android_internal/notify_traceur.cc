/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <unistd.h>

int main() {
  char argv0[] = "am";
  char argv1[] = "start-foreground-service";
  char argv2[] = "-n";
  char argv3[] = "com.android.traceur/.TraceService";
  char argv4[] = "-a";
  char argv5[] = "com.android.traceur.STOP_TRACING";
  char* argv[] = {argv0, argv1, argv2, argv3, argv4, argv4, argv5, nullptr};
  execvp(argv0, argv);
  _exit(3);
}
