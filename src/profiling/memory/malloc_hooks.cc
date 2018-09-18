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
#include <sys/socket.h>
#include <sys/un.h>

#include <private/bionic_malloc_dispatch.h>
#include "perfetto/base/scoped_file.h"

static const MallocDispatch* g_dispatch;

__BEGIN_DECLS

bool heapprofd_initialize(const MallocDispatch* malloc_dispatch,
                          int* malloc_zygote_child,
                          const char* options);
void heapprofd_finalize();
void heapprofd_dump_heap(const char* file_name);
void heapprofd_get_malloc_leak_info(uint8_t** info,
                                    size_t* overall_size,
                                    size_t* info_size,
                                    size_t* total_memory,
                                    size_t* backtrace_size);
bool heapprofd_write_malloc_leak_info(FILE* fp);
ssize_t heapprofd_malloc_backtrace(void* pointer,
                                   uintptr_t* frames,
                                   size_t frame_count);
void heapprofd_free_malloc_leak_info(uint8_t* info);
size_t heapprofd_malloc_usable_size(void* pointer);
void* heapprofd_malloc(size_t size);
void heapprofd_free(void* pointer);
void* heapprofd_aligned_alloc(size_t alignment, size_t size);
void* heapprofd_memalign(size_t alignment, size_t bytes);
void* heapprofd_realloc(void* pointer, size_t bytes);
void* heapprofd_calloc(size_t nmemb, size_t bytes);
struct mallinfo heapprofd_mallinfo();
int heapprofd_mallopt(int param, int value);
int heapprofd_posix_memalign(void** memptr, size_t alignment, size_t size);
int heapprofd_iterate(uintptr_t base,
                      size_t size,
                      void (*callback)(uintptr_t base, size_t size, void* arg),
                      void* arg);
void heapprofd_malloc_disable();
void heapprofd_malloc_enable();

#if defined(HAVE_DEPRECATED_MALLOC_FUNCS)
void* heapprofd_pvalloc(size_t bytes);
void* heapprofd_valloc(size_t size);
#endif

__END_DECLS

bool heapprofd_initialize(const MallocDispatch* malloc_dispatch,
                          int*,
                          const char*) {
  g_dispatch = malloc_dispatch;
  return true;
}

void heapprofd_finalize() {}

void heapprofd_dump_heap(const char*) {}

void heapprofd_get_malloc_leak_info(uint8_t**,
                                    size_t*,
                                    size_t*,
                                    size_t*,
                                    size_t*) {}

bool heapprofd_write_malloc_leak_info(FILE*) {
  return false;
}

ssize_t heapprofd_malloc_backtrace(void*, uintptr_t*, size_t) {
  return -1;
}

void heapprofd_free_malloc_leak_info(uint8_t*) {}

size_t heapprofd_malloc_usable_size(void* pointer) {
  return g_dispatch->malloc_usable_size(pointer);
}

void* heapprofd_malloc(size_t size) {
  return g_dispatch->malloc(size);
}

void heapprofd_free(void* pointer) {
  return g_dispatch->free(pointer);
}

void* heapprofd_aligned_alloc(size_t alignment, size_t size) {
  return g_dispatch->aligned_alloc(alignment, size);
}

void* heapprofd_memalign(size_t alignment, size_t bytes) {
  return g_dispatch->memalign(alignment, bytes);
}

void* heapprofd_realloc(void* pointer, size_t bytes) {
  return g_dispatch->realloc(pointer, bytes);
}

void* heapprofd_calloc(size_t nmemb, size_t bytes) {
  return g_dispatch->calloc(nmemb, bytes);
}

struct mallinfo heapprofd_mallinfo() {
  return g_dispatch->mallinfo();
}

int heapprofd_mallopt(int param, int value) {
  return g_dispatch->mallopt(param, value);
}

int heapprofd_posix_memalign(void** memptr, size_t alignment, size_t size) {
  return g_dispatch->posix_memalign(memptr, alignment, size);
}

int heapprofd_iterate(uintptr_t,
                      size_t,
                      void (*)(uintptr_t base, size_t size, void* arg),
                      void*) {
  return 0;
}

void heapprofd_malloc_disable() {
  return g_dispatch->malloc_disable();
}

void heapprofd_malloc_enable() {
  return g_dispatch->malloc_enable();
}

#if defined(HAVE_DEPRECATED_MALLOC_FUNCS)
void* heapprofd_pvalloc(size_t bytes) {
  return g_dispatch->palloc(size);
}

void* heapprofd_valloc(size_t size) {
  return g_dispatch->valloc(size);
}

#endif
