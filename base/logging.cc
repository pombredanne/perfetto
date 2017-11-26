/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace perfetto {
namespace base {

namespace {

#if !defined(NDEBUG)

constexpr size_t kDemangledNameLen = 1024;

bool g_sighandler_registered = false;
char* g_demangled_name = nullptr;

template <typename T>
void Print(const T& str) {
  write(STDERR_FILENO, str, sizeof(str));
}

template <typename T>
void PrintHex(T n) {
  for (unsigned i = 0; i < sizeof(n) * 8; i += 4) {
    char nibble = static_cast<char>(n >> (sizeof(n) * 8 - i - 4)) & 0x0F;
    char c = (nibble < 10) ? '0' + nibble : 'A' + nibble - 10;
    write(STDERR_FILENO, &c, 1);
  }
}

// Note: use only async-safe functions inside this.
void SignalHandler(int sig_num, siginfo_t* info, void* ucontext) {
  Print("------------------ BEGINNING OF CRASH ------------------\n");
  Print("Signal: ");
  if (sig_num == SIGSEGV)
    Print("Segmentation fault");
  else if (sig_num == SIGILL)
    Print("Illegal instruction (possibly unaligned access)");
  else if (sig_num == SIGTRAP)
    Print("Trap");
  else if (sig_num == SIGABRT)
    Print("Abort");
  else if (sig_num == SIGBUS)
    Print("Bus Error (possibly unmapped memory access)");
  else if (sig_num == SIGFPE)
    Print("Floating point exception");
  else
    Print("Unexpected signal");

  Print("\n");

  Print("Fault addr: ");
  PrintHex(reinterpret_cast<uintptr_t>(info->si_addr));
  Print("\n\nBacktrace:\n");

  const int kMaxFrames = 32;
  void* buffer[kMaxFrames];
  const int nptrs = backtrace(buffer, kMaxFrames);
  // char** symbols = backtrace_symbols(buffer, nptrs);

  for (uint8_t i = 0; i < nptrs; i++) {
    Dl_info sym_info = {};
    int res = dladdr(buffer[i], &sym_info);
    Print("#");
    PrintHex(i);
    Print("  ");
    if (res) {
      const char* sym_name = sym_info.dli_sname;
      int ignored;
      size_t len = kDemangledNameLen;
      char* demangled = abi::__cxa_demangle(sym_info.dli_sname,
                                            g_demangled_name, &len, &ignored);
      if (demangled) {
        sym_name = demangled;
        // In the exceptional case of demangling someting > kDemangledNameLen,
        // __cxa_demangle will realloc(). In that case the malloc()-ed pointer
        // might be moved.
        g_demangled_name = demangled;
      }
      write(STDERR_FILENO, sym_name, strlen(sym_name));
    } else {
      Print("???");
    }
    Print("\n\n");
  }

  Print("------------------ END OF CRASH ------------------\n");
}

void __attribute__((constructor)) EnableStacktraceOnCrashForDebug() {
  if (g_sighandler_registered)
    return;

  // Pre-allocate the string for __cxa_demangle() to reduce the risk of that
  // invoking realloc() within the signal handler.
  g_demangled_name = reinterpret_cast<char*>(malloc(kDemangledNameLen));
  struct sigaction sigact;
  sigact.sa_sigaction = &SignalHandler;
  sigact.sa_flags = SA_RESTART | SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGSEGV, &sigact, nullptr);
  sigaction(SIGILL, &sigact, nullptr);
  sigaction(SIGTRAP, &sigact, nullptr);
  sigaction(SIGABRT, &sigact, nullptr);
  sigaction(SIGBUS, &sigact, nullptr);
  sigaction(SIGFPE, &sigact, nullptr);
}

#endif  // !NDEBUG

}  // namespace
}  // namespace base
}  // namespace perfetto
