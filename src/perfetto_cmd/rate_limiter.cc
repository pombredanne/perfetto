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
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/utils.h"

namespace perfetto {

const char kStatePath[] = "/data/misc/perfetto-traces/.guardraildata";

RateLimiter::RateLimiter() = default;
RateLimiter::~RateLimiter() = default;

bool RateLimiter::ShouldTrace(const Args& args) {
  // Not uploading?
  // -> We can just trace.
  if (!args.is_dropbox)
    return true;

  bool loaded_state = LoadState(&state_);

  // Failed to load the state?
  // Current time is before either saved times?
  // Last saved trace time is before first saved trace time?
  // -> Try to save a clean state but don't trace.
  if (!loaded_state ||
      args.current_timestamp < state_.first_trace_timestamp() ||
      args.current_timestamp < state_.last_trace_timestamp() ||
      state_.last_trace_timestamp() < state_.first_trace_timestamp()) {
    PerfettoCmdState output{};
    SaveState(output);
    PERFETTO_ELOG("Guardrail: guardrail state invalid.");
    return false;
  }

  // If we've uploaded in the last 5mins we shouldn't trace now.
  if ((args.current_timestamp - state_.last_trace_timestamp()) < 60 * 5) {
    PERFETTO_ELOG("Guardrail: Uploaded to DropBox in the last 5mins.");
    if (!args.ignore_guardrails)
      return false;
  }

  // First trace was more than 24h ago? Reset state.
  if ((args.current_timestamp - state_.first_trace_timestamp()) >
      60 * 60 * 24) {
    state_.set_first_trace_timestamp(0);
    state_.set_last_trace_timestamp(0);
    state_.set_total_bytes_uploaded(0);
  }

  // If we've uploaded more than 10mb in the last 24 hours we shouldn't trace
  // now.
  if (state_.total_bytes_uploaded() > 10 * 1024 * 1024) {
    PERFETTO_ELOG("Guardrail: Uploaded >10mb DropBox in the last 24h.");
    if (!args.ignore_guardrails)
      return false;
  }

  return true;
}

bool RateLimiter::TraceDone(const Args& args, bool success, size_t bytes) {
  // Failed to upload? Don't update the state.
  if (!success)
    return false;

  if (!args.is_dropbox)
    return true;

  // If the first trace timestamp is 0 (either because this is the
  // first time or because it was reset for being more than 24h ago).
  // -> We update it to the time of this trace.
  if (state_.first_trace_timestamp() == 0)
    state_.set_first_trace_timestamp(args.current_timestamp);
  // Always updated the last trace timestamp.
  state_.set_last_trace_timestamp(args.current_timestamp);
  // Add the amount we uploded to the running total.
  state_.set_total_bytes_uploaded(state_.total_bytes_uploaded() + bytes);

  return SaveState(state_);
}

bool RateLimiter::LoadState(PerfettoCmdState* state) {
  base::ScopedFile fd;
  fd.reset(open(kStatePath, O_RDONLY | O_CREAT, 0600));
  return ReadState(fd.get(), state);
}

bool RateLimiter::SaveState(const PerfettoCmdState& state) {
  base::ScopedFile fd;
  fd.reset(open(kStatePath, O_WRONLY | O_CREAT, 0600));
  return WriteState(fd.get(), state);
}

// static
bool RateLimiter::ReadState(int in_fd, PerfettoCmdState* state) {
  if (in_fd == -1)
    return false;
  char buf[1024];
  ssize_t bytes = read(in_fd, &buf, sizeof(buf));
  if (bytes < 0)
    return false;
  return state->ParseFromArray(&buf, bytes);
}

// static
bool RateLimiter::WriteState(int out_fd, const PerfettoCmdState& state) {
  if (out_fd == -1)
    return false;
  char buf[1024];
  size_t size = state.ByteSize();
  if (!state.SerializeToArray(&buf, size))
    return false;

  ssize_t written = write(out_fd, &buf, size);
  return written >= 0 && static_cast<size_t>(written) == size;
}

}  // namespace perfetto
