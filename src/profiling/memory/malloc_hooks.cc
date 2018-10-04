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

#include <inttypes.h>
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>

#include <atomic>

#include <private/bionic_malloc_dispatch.h>

#include "perfetto/base/build_config.h"
#include "src/profiling/memory/client.h"

static std::atomic<const MallocDispatch*> g_dispatch{nullptr};
static std::atomic<perfetto::Client*> g_client{nullptr};
static constexpr const char* kHeapprofdSock = "/dev/socket/heapprofd";
static constexpr size_t kNumConnections = 2;

static constexpr std::memory_order write_order = std::memory_order_release;
static constexpr std::memory_order read_order = std::memory_order_relaxed;

// This is so we can make an so that we can swap out with the existing
// libc_malloc_hooks.so
#ifndef HEAPPROFD_PREFIX
#define HEAPPROFD_PREFIX heapprofd
#endif

#define HEAPPROFD_ADD_PREFIX(name) \
  PERFETTO_BUILDFLAG_CAT(HEAPPROFD_PREFIX, name)

#pragma GCC visibility push(default)
__BEGIN_DECLS

bool HEAPPROFD_ADD_PREFIX(_initialize)(const MallocDispatch* malloc_dispatch,
                                       int* malloc_zygote_child,
                                       const char* options);
void HEAPPROFD_ADD_PREFIX(_finalize)();
void HEAPPROFD_ADD_PREFIX(_dump_heap)(const char* file_name);
void HEAPPROFD_ADD_PREFIX(_get_malloc_leak_info)(uint8_t** info,
                                                 size_t* overall_size,
                                                 size_t* info_size,
                                                 size_t* total_memory,
                                                 size_t* backtrace_size);
bool HEAPPROFD_ADD_PREFIX(_write_malloc_leak_info)(FILE* fp);
ssize_t HEAPPROFD_ADD_PREFIX(_malloc_backtrace)(void* pointer,
                                                uintptr_t* frames,
                                                size_t frame_count);
void HEAPPROFD_ADD_PREFIX(_free_malloc_leak_info)(uint8_t* info);
size_t HEAPPROFD_ADD_PREFIX(_malloc_usable_size)(void* pointer);
void* HEAPPROFD_ADD_PREFIX(_malloc)(size_t size);
void HEAPPROFD_ADD_PREFIX(_free)(void* pointer);
void* HEAPPROFD_ADD_PREFIX(_aligned_alloc)(size_t alignment, size_t size);
void* HEAPPROFD_ADD_PREFIX(_memalign)(size_t alignment, size_t bytes);
void* HEAPPROFD_ADD_PREFIX(_realloc)(void* pointer, size_t bytes);
void* HEAPPROFD_ADD_PREFIX(_calloc)(size_t nmemb, size_t bytes);
struct mallinfo HEAPPROFD_ADD_PREFIX(_mallinfo)();
int HEAPPROFD_ADD_PREFIX(_mallopt)(int param, int value);
int HEAPPROFD_ADD_PREFIX(_posix_memalign)(void** memptr,
                                          size_t alignment,
                                          size_t size);
int HEAPPROFD_ADD_PREFIX(_iterate)(uintptr_t base,
                                   size_t size,
                                   void (*callback)(uintptr_t base,
                                                    size_t size,
                                                    void* arg),
                                   void* arg);
void HEAPPROFD_ADD_PREFIX(_malloc_disable)();
void HEAPPROFD_ADD_PREFIX(_malloc_enable)();

#if defined(HAVE_DEPRECATED_MALLOC_FUNCS)
void* HEAPPROFD_ADD_PREFIX(_pvalloc)(size_t bytes);
void* HEAPPROFD_ADD_PREFIX(_valloc)(size_t size);
#endif

__END_DECLS
#pragma GCC visibility pop

bool HEAPPROFD_ADD_PREFIX(_initialize)(const MallocDispatch* malloc_dispatch,
                                       int*,
                                       const char*) {
  g_dispatch.store(malloc_dispatch, write_order);
  g_client.store(new perfetto::Client(kHeapprofdSock, kNumConnections),
                 write_order);
  return true;
}

void HEAPPROFD_ADD_PREFIX(_finalize)() {
  g_client.store(nullptr, write_order);
}

void HEAPPROFD_ADD_PREFIX(_dump_heap)(const char*) {}

void HEAPPROFD_ADD_PREFIX(
    _get_malloc_leak_info)(uint8_t**, size_t*, size_t*, size_t*, size_t*) {}

bool HEAPPROFD_ADD_PREFIX(_write_malloc_leak_info)(FILE*) {
  return false;
}

ssize_t HEAPPROFD_ADD_PREFIX(_malloc_backtrace)(void*, uintptr_t*, size_t) {
  return -1;
}

void HEAPPROFD_ADD_PREFIX(_free_malloc_leak_info)(uint8_t*) {}

size_t HEAPPROFD_ADD_PREFIX(_malloc_usable_size)(void* pointer) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  return dispatch->malloc_usable_size(pointer);
}

void* HEAPPROFD_ADD_PREFIX(_malloc)(size_t size) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  perfetto::Client* client = g_client.load(read_order);
  void* addr = dispatch->malloc(size);
  if (client &&
      client->ShouldSampleAlloc(size, dispatch->malloc, dispatch->free))
    client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

void HEAPPROFD_ADD_PREFIX(_free)(void* pointer) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  perfetto::Client* client = g_client.load(read_order);
  if (client)
    client->RecordFree(reinterpret_cast<uint64_t>(pointer));
  return dispatch->free(pointer);
}

void* HEAPPROFD_ADD_PREFIX(_aligned_alloc)(size_t alignment, size_t size) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  perfetto::Client* client = g_client.load(read_order);
  void* addr = dispatch->aligned_alloc(alignment, size);
  if (client &&
      client->ShouldSampleAlloc(size, dispatch->malloc, dispatch->free))
    client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

void* HEAPPROFD_ADD_PREFIX(_memalign)(size_t alignment, size_t size) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  perfetto::Client* client = g_client.load(read_order);
  void* addr = dispatch->memalign(alignment, size);
  if (client &&
      client->ShouldSampleAlloc(size, dispatch->malloc, dispatch->free))
    client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

void* HEAPPROFD_ADD_PREFIX(_realloc)(void* pointer, size_t size) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  perfetto::Client* client = g_client.load(read_order);
  if (client)
    client->RecordFree(reinterpret_cast<uint64_t>(pointer));
  void* addr = dispatch->realloc(pointer, size);
  if (client &&
      client->ShouldSampleAlloc(size, dispatch->malloc, dispatch->free))
    client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

void* HEAPPROFD_ADD_PREFIX(_calloc)(size_t nmemb, size_t size) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  perfetto::Client* client = g_client.load(read_order);
  void* addr = dispatch->calloc(nmemb, size);
  if (client &&
      client->ShouldSampleAlloc(size, dispatch->malloc, dispatch->free))
    client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

struct mallinfo HEAPPROFD_ADD_PREFIX(_mallinfo)() {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  return dispatch->mallinfo();
}

int HEAPPROFD_ADD_PREFIX(_mallopt)(int param, int value) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  return dispatch->mallopt(param, value);
}

int HEAPPROFD_ADD_PREFIX(_posix_memalign)(void** memptr,
                                          size_t alignment,
                                          size_t size) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  return dispatch->posix_memalign(memptr, alignment, size);
}

int HEAPPROFD_ADD_PREFIX(_iterate)(uintptr_t,
                                   size_t,
                                   void (*)(uintptr_t base,
                                            size_t size,
                                            void* arg),
                                   void*) {
  return 0;
}

void HEAPPROFD_ADD_PREFIX(_malloc_disable)() {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  return dispatch->malloc_disable();
}

void HEAPPROFD_ADD_PREFIX(_malloc_enable)() {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  return dispatch->malloc_enable();
}

#if defined(HAVE_DEPRECATED_MALLOC_FUNCS)
void* HEAPPROFD_ADD_PREFIX(_pvalloc)(size_t size) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  return dispatch->pvalloc(size);
}

void* HEAPPROFD_ADD_PREFIX(_valloc)(size_t size) {
  const MallocDispatch* dispatch = g_dispatch.load(read_order);
  return dispatch->valloc(size);
}

#endif
