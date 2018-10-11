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
  // Create() call.
  PerfettoConsumer_kConfigured = 2,

  // Tracing is active.
  PerfettoConsumer_kTracing = 3,

  // Tracing ended succesfully. The trace buffer can now be retrieved through
  // the ReadTrace() call.
  PerfettoConsumer_kTraceEnded = 4,
};

typedef int PerfettoConsumer_Handle;
const int PerfettoConsumer_kInvalidHandle = -1;

// Signature for callback function provided by the embedder to get notified
// about state changes.
typedef void (*PerfettoConsumer_OnStateChangedCb)(PerfettoConsumer_Handle,
                                                  PerfettoConsumer_State);

// None of the calls below are blocking, unless otherwise specified.

// Enables tracing with the given TraceConfig. If the trace config has the
// |deferred_start| flag set (see trace_config.proto) tracing is initialized
// but not started. An explicit call to StartTracing() must be issued in order
// to start the capture.
// Args:
//   [trace_config_proto, trace_config_len] point to a binary-encoded proto
//     containing the trace config. See //external/perfetto/docs/trace-config.md
//     for more details.
//   callback: a user-defined callback that will be invoked upon state changes.
//     The callback will be invoked on an internal thread and must not block.
// Return value:
//    Returns a handle that can be used to poll, wait and retrieve the trace or
//    kInvalidHandle in case of failure (e.g., the trace config is malformed).
//    The returned handle is a valid file descriptor and can be passed to
//    poll()/select() to be notified about the end of the trace (useful when
//    the ReadTrace() cannot be used in blocking mode).
//    Do NOT directly close() the handle, use Destroy(handle) to do so. The
//    client maintains other state associated to the handle that would be leaked
//    otherwise.
PerfettoConsumer_Handle PerfettoConsumer_Create(
    const void* config_proto,
    size_t config_len,
    PerfettoConsumer_OnStateChangedCb callback);

// Starts recording the trace. Can be used only when setting the
// |deferred_start| flag in the trace config passed to Create().
// The estimated end-to-end (this call to ftrace enabling) latency is 2-3 ms
// on a Pixel 2.
// This method can be called only once per handle. TODO(primiano): relax this
// and allow to recycle handles without re-configuring the trace session.
void PerfettoConsumer_StartTracing(PerfettoConsumer_Handle);

// Returns the state of the tracing session (for debugging).
PerfettoConsumer_State PerfettoConsumer_PollState(PerfettoConsumer_Handle);

struct PerfettoConsumer_TraceBuffer {
  PerfettoConsumer_State state;
  char* begin;
  size_t size;
};

// Retrieves the whole trace buffer. It avoids extra copies by directly mmaping
// the tmp fd passed to the traced daemon.
// Return value:
//   If the trace is ended (state == kTraceEnded) returns a buffer containing
//   the whole trace. This buffer can be parsed directly with libprotobuf.
//   The buffer lifetime is tied to the tracing session and is valid until the
//   Destroy() call.
//   If called before the session reaches the kTraceEnded state, a null buffer
//   is returned and the current session state is set in the |state| field.
PerfettoConsumer_TraceBuffer PerfettoConsumer_ReadTrace(
    PerfettoConsumer_Handle);

// Destroys all the resources associated to the tracing session (connection to
// traced and trace buffer). The handle should not be used after this point.
void PerfettoConsumer_Destroy(PerfettoConsumer_Handle);

}  // extern "C".

#endif  // INCLUDE_PERFETTO_PUBLIC_CONSUMER_API_H_
