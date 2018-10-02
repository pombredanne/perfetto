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

#ifndef INCLUDE_PERFETTO_PUBLIC_CONSUMER_API_H_
#define INCLUDE_PERFETTO_PUBLIC_CONSUMER_API_H_

#include <stddef.h>

extern "C" {

// Enables tracing with the given TraceConfig. If the trace config has the
// |deferred_start| flag set (see trace_config.proto) tracing is initialized
// but not started. An explicit call to StartTracing() must be issued in order
// to start the capture.
// The caller must take ownership of the returned pipe. The returned pipe serves
// also to scope the lifetime of the tracing session.
// Args:
//   [trace_config_proto, trace_config_len] point to a binary-encoded proto
//   containing the trace config. See //external/perfetto/docs/trace-config.md
//   for more details.
// Return value:
//    Returns the reading end of a pipe that can be used to read the trace from
//    any thread. The pipe will be closed (so reading it will return 0 -> EOF)
//    when the trace ended or an error occured. Errors will be logged to stderr
//    and logcat.
//    The pipe can be read during the trace (streaming mode) or at the end. In
//    the latter case, the pipe will be backed by the tracing userspace ring
//    buffer(s).
//    If the read end is closed before tracing ends, the tracing session will
//    be terminated prematurely.
int PerfettoConsumer_EnableTracing(const void* trace_config_proto,
                                   size_t trace_config_len);

// Starts recording the trace. Can be used only when setting the
// |deferred_start| flag in the trace config passed to EnableTracing().
// The estimated end-to-end (this call to ftrace enabling) latency is 2-3 ms
// on a Pixel 2.
void PerfettoConsumer_StartTracing();

// TODO seems unnecessary here unless we want to expose a PollState() method.
enum PerfettoConsumer_State {
  PerfettoConsumer_kDisconnected = 0,
  PerfettoConsumer_kEnabled = 0,
};

}  // extern "C".

#endif  // INCLUDE_PERFETTO_PUBLIC_CONSUMER_API_H_
