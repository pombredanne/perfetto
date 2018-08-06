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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

timer_t timerid;

void sigfunction(union sigval) {
  printf("Hello from thread.\n");
}

void SignalHandler(int) {
  struct itimerspec its = {};
  its.it_value.tv_nsec = 1;
  if (timer_settime(timerid, 0, &its, nullptr) == -1)
    abort();
}

int main(int argc, char** argv) {
  struct sigevent sev = {};
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = sigfunction;

  if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
    printf("Failed to timer.\n");
    abort();
  }

  signal(SIGUSR1, SignalHandler);
  for (;;) {
  }
}
