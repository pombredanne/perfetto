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

enum PerfettoConsumer_State {
  // The trace session failed. Look at logcat -s perfetto to find out more.
  PerfettoConsumer_kTraceFailed = -3,

  // Failed to connect to the traced daemon.
  PerfettoConsumer_kConnectionError = -2,

  // The passed handle is invalid.
  PerfettoConsumer_kSessionNotFound = -1,

  // Idle state (should never be returned, internal only).
  PerfettoConsumer_kIdle = 0,

  // Establishing the connection to the traced daemon.
  PerfettoConsumer_kConnecting = 1,

  // Tracing configured (buffers allocated) but not started.
  // This state is reached only when setting |deferred_start| == true,
  // otherwise the session transitions immediately into kTracing after the
  // EnableTracing() call.
  PerfettoConsumer_kConfigured = 2,

  // Tracing is active.
  PerfettoConsumer_kTracing = 3,

  // Tracing ended succesfully. The trace buffer can now be retrieved through
  // the ReadTrace() call.
  PerfettoConsumer_kTraceEnded = 4,
};

typedef int PerfettoConsumer_Handle;
const int PerfettoConsumer_kInvalidHandle = -1;

// Enables tracing with the given TraceConfig. If the trace config has the
// |deferred_start| flag set (see trace_config.proto) tracing is initialized
// but not started. An explicit call to StartTracing() must be issued in order
// to start the capture.
// Args:
//   [trace_config_proto, trace_config_len] point to a binary-encoded proto
//   containing the trace config. See //external/perfetto/docs/trace-config.md
//   for more details.
// Return value:
//    Returns a handle that can be used to poll, wait and retrieve the trace or
//    kInvalidHandle in case of failure (e.g., the trace config is malformed).
//    The returned handle is a valid file descriptor and can be passed to
//    poll()/select() to be notified about the end of the trace (useful when
//    the ReadTrace() cannot be used in blocking mode).
//    Do NOT directly close() the handle, use Destroy(handle) to do so. The
//    client maintains other state associated to the handle that would be leaked
//    otherwise.
PerfettoConsumer_Handle PerfettoConsumer_EnableTracing(const void* config_proto,
                                                       size_t config_len);

// Starts recording the trace. Can be used only when setting the
// |deferred_start| flag in the trace config passed to EnableTracing().
// The estimated end-to-end (this call to ftrace enabling) latency is 2-3 ms
// on a Pixel 2.
void PerfettoConsumer_StartTracing(PerfettoConsumer_Handle);

// Returns the state of the tracing session.
PerfettoConsumer_State PerfettoConsumer_PollState(PerfettoConsumer_Handle);

struct PerfettoConsumer_TraceBuffer {
  char* begin;
  size_t size;
};

// Retrieves the trace buffer. This call can be used both in blocking and
// non-blocking mode.
// Return value:
//   If the trace is ended (PollState() returned kTraceEnded) returns a buffer
//   containing the whole trace. The buffer lifetime is tied to the traing
//   session is owned by the library and is valid until the Destroy() call.
//   If the trace is not ended (or failed) returns a zero-sized null buffer.
// Args:
//   |wait_ms| > 0: waits for the trace to be ended, blocking for at most X ms.
//   |wait_ms| = 0: returns immediately, either the full buffer or a
//   zero-sized one if the trace is not ended yet.
PerfettoConsumer_TraceBuffer PerfettoConsumer_ReadTrace(PerfettoConsumer_Handle,
                                                        int wait_ms);

// Destroys all the resources associated to the tracing session (connection to
// traced and trace buffer). The handle should not be used after this point.
void PerfettoConsumer_Destroy(PerfettoConsumer_Handle);

}  // extern "C".

#endif  // INCLUDE_PERFETTO_PUBLIC_CONSUMER_API_H_
