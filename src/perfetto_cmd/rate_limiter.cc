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

#include "rate_limiter.h"

#include "perfetto/base/logging.h"

namespace perfetto {

RateLimiter::RateLimiter(Delegate* delegate) : delegate_(delegate) {}
RateLimiter::~RateLimiter() = default;

int RateLimiter::Run(const Args& args) {
  // Not uploading?
  // -> We can just trace.
  if (!args.is_dropbox)
    return delegate_->DoTrace() ? 0 : 1;

  PerfettoCmdState state{};
  bool loaded_state = delegate_->LoadState(&state);

  // Failed to load the state?
  // Current time is before either saved times?
  // Last saved trace time is before first saved trace time?
  // -> Try to save a clean state but don't trace.
  if (!loaded_state || args.current_timestamp < state.first_trace_timestamp() ||
      args.current_timestamp < state.last_trace_timestamp() ||
      state.last_trace_timestamp() < state.first_trace_timestamp()) {
    PerfettoCmdState output{};
    delegate_->SaveState(output);
    PERFETTO_ELOG("Guardrail: guardrail state invalid.");
    return 1;
  }

  // If we've uploaded in the last 5mins we shouldn't trace now.
  if ((args.current_timestamp - state.last_trace_timestamp()) < 60 * 5) {
    PERFETTO_ELOG("Guardrail: Uploaded to DropBox in the last 5mins.");
    if (!args.ignore_guardrails)
      return 1;
  }

  // First trace was more than 24h ago? Reset state.
  if ((args.current_timestamp - state.first_trace_timestamp()) > 60 * 60 * 24) {
    state.set_first_trace_timestamp(0);
    state.set_last_trace_timestamp(0);
    state.set_total_bytes_uploaded(0);
  }

  // If we've uploaded more than 10mb in the last 24 hours we shouldn't trace
  // now.
  if (state.total_bytes_uploaded() > 10 * 1024 * 1024) {
    PERFETTO_ELOG("Guardrail: Uploaded >10mb DropBox in the last 24h.");
    if (!args.ignore_guardrails)
      return 1;
  }

  uint64_t uploaded = 0;
  bool success = delegate_->DoTrace(&uploaded);

  // Failed to upload? Don't update the state.
  if (!success)
    return 1;

  // If the first trace timestamp is 0 (either because this is the
  // first time or because it was reset for being more than 24h ago).
  // -> We update it to the time of this trace.
  if (state.first_trace_timestamp() == 0)
    state.set_first_trace_timestamp(args.current_timestamp);
  // Always updated the last trace timestamp.
  state.set_last_trace_timestamp(args.current_timestamp);
  // Add the amount we uploded to the running total.
  state.set_total_bytes_uploaded(state.total_bytes_uploaded() + uploaded);

  bool save_success = delegate_->SaveState(state);

  return save_success ? 0 : 1;
}

}  // namespace perfetto
