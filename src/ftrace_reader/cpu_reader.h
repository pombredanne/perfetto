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

#ifndef SRC_FTRACE_READER_CPU_READER_H_
#define SRC_FTRACE_READER_CPU_READER_H_

#include <stdint.h>
#include <string.h>

#include <array>
#include <memory>
#include <mutex>
#include <thread>

#include "gtest/gtest_prod.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/ftrace_reader/ftrace_controller.h"
#include "perfetto/protozero/protozero_message.h"
#include "proto_translation_table.h"

namespace perfetto {

class ProtoTranslationTable;

namespace protos {
namespace pbzero {
class FtraceEventBundle;
}  // namespace pbzero
}  // namespace protos

// Class for efficient 'is event with id x enabled?' tests.
// Mirrors the data in a FtraceConfig but in a format better suited
// to be consumed by CpuReader.
class EventFilter {
 public:
  EventFilter(const ProtoTranslationTable&, std::set<std::string>);
  ~EventFilter();

  bool IsEventEnabled(size_t ftrace_event_id) const {
    if (ftrace_event_id == 0 || ftrace_event_id > enabled_ids_.size()) {
      return false;
    }
    return enabled_ids_[ftrace_event_id];
  }

  const std::set<std::string>& enabled_names() const { return enabled_names_; }

 private:
  EventFilter(const EventFilter&) = delete;
  EventFilter& operator=(const EventFilter&) = delete;

  const std::vector<bool> enabled_ids_;
  std::set<std::string> enabled_names_;
};

// Processes raw ftrace data for a logical CPU core. Data can be read in one of
// two ways:
//
// 1) Calling Drain() on a fixed schedule, i.e., by polling.
//
// 2) Calling WaitForData() to wait until a full page of data is
//    available, followed by a call to Drain().
//
// Internally the first option is implemented by directly reading the raw trace
// pipe directly, while the second option uses splice(2) to move data from
// the trace ring buffer to an intermediate pipe. This lets us block inside the
// splice syscall until a full page of data is available -- as opposed to
// poll(2) which would return as soon as any data is available to read.
//
class CpuReader {
 public:
  CpuReader(base::TaskRunner*,
            const ProtoTranslationTable*,
            size_t cpu,
            base::ScopedFile fd);
  ~CpuReader();

  // Starts waiting for ftrace data be available. |callback| will be repeatedly
  // invoked on the task runner passed to the constructor.
  void WaitForData(std::function<void(void)> callback, int delay_ms = 0);

  // Read up to a page of data an ftrace buffer without blocking. Returns true
  // if a page was read successfully and it was more than half populated with
  // ftrace events.
  bool Drain(
      const std::array<const EventFilter*, kMaxSinks>&,
      const std::array<
          protozero::ProtoZeroMessageHandle<protos::pbzero::FtraceEventBundle>,
          kMaxSinks>&);

  // Estimates how much data this CPU is producing based on the drain rate.
  float pages_per_second() const { return pages_per_second_; }

  template <typename T>
  static bool ReadAndAdvance(const uint8_t** ptr, const uint8_t* end, T* out) {
    if (*ptr > end - sizeof(T))
      return false;
    memcpy(reinterpret_cast<void*>(out), reinterpret_cast<const void*>(*ptr),
           sizeof(T));
    *ptr += sizeof(T);
    return true;
  }

  // Caller must do the bounds check:
  // [start + offset, start + offset + sizeof(T))
  template <typename T>
  static void ReadIntoVarInt(const uint8_t* start,
                             size_t field_id,
                             protozero::ProtoZeroMessage* out) {
    T t;
    memcpy(&t, reinterpret_cast<const void*>(start), sizeof(T));
    out->AppendVarInt<T>(field_id, t);
  }

  // Parse a raw ftrace page beginning at ptr and write the events a protos
  // into the provided bundle respecting the given event filter.
  // |table| contains the mix of compile time (e.g. proto field ids) and
  // run time (e.g. field offset and size) information necessary to do this.
  // The table is initialized once at start time by the ftrace controller
  // which passes it to the CpuReader which passes it here.
  static size_t ParsePage(size_t cpu,
                          const uint8_t* ptr,
                          const EventFilter*,
                          protos::pbzero::FtraceEventBundle*,
                          const ProtoTranslationTable* table);

  // Parse a single raw ftrace event beginning at |start| and ending at |end|
  // and write it into the provided bundle as a proto.
  // |table| contains the mix of compile time (e.g. proto field ids) and
  // run time (e.g. field offset and size) information necessary to do this.
  // The table is initialized once at start time by the ftrace controller
  // which passes it to the CpuReader which passes it to ParsePage which
  // passes it here.
  static bool ParseEvent(uint16_t ftrace_event_id,
                         const uint8_t* start,
                         const uint8_t* end,
                         const ProtoTranslationTable* table,
                         protozero::ProtoZeroMessage* message);

  static bool ParseField(const Field& field,
                         const uint8_t* start,
                         const uint8_t* end,
                         protozero::ProtoZeroMessage* message);

 private:
  uint8_t* GetBuffer();
  CpuReader(const CpuReader&) = delete;
  CpuReader& operator=(const CpuReader&) = delete;

  const ProtoTranslationTable* table_;
  const size_t cpu_;
  base::ScopedFile trace_fd_;
  base::ScopedFile staging_read_fd_;
  base::ScopedFile staging_write_fd_;
  size_t staging_capacity_;
  std::unique_ptr<uint8_t[]> buffer_;

  base::UnixTaskRunner monitor_task_runner_;
  std::thread monitor_thread_;
  int64_t last_read_time_ns_ = 0;
  float pages_per_second_ = 0.f;

  // Whether we have used splice(2) to move data from the trace fd into the
  // staging pipe.
  int pages_in_staging_pipe_ = 0;

  // Begin lock protected members.
  std::mutex lock_;
  base::TaskRunner* task_runner_;
  // End lock protected members.
};

}  // namespace perfetto

#endif  // SRC_FTRACE_READER_CPU_READER_H_
