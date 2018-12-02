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

#include "src/traced/probes/power/android_power_data_source.h"

#include <dlfcn.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/optional.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "src/android_hal/health_hal.h"

#include "perfetto/trace/power/battery_counters.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {

decltype(&android_hal::GetBatteryCounter) GetBatteryCounterFunction() {
  void* handle = dlopen("libperfetto_hal.so", RTLD_NOW);
  if (!handle) {
    PERFETTO_PLOG("dlopen() failed");
    return nullptr;
  }
  void* fn = dlsym(handle, "GetBatteryCounter");
  if (!fn)
    PERFETTO_PLOG("dlsym() failed");
  return reinterpret_cast<decltype(&android_hal::GetBatteryCounter)>(fn);
}

base::Optional<int64_t> GetCounter(android_hal::BatteryCounter counter) {
  auto get_counter_fn = GetBatteryCounterFunction();
  if (!get_counter_fn)
    return base::nullopt;
  int64_t value = 0;
  if (get_counter_fn(counter, &value))
    return base::make_optional(value);
  return base::nullopt;
}

}  // namespace

AndroidPowerDataSource::AndroidPowerDataSource(
    DataSourceConfig cfg,
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, kTypeId),
      task_runner_(task_runner),
      poll_rate_ms_(cfg.android_power_config().battery_poll_ms()),
      writer_(std::move(writer)),
      weak_factory_(this) {
  poll_rate_ms_ = std::min(poll_rate_ms_, 100u);
  for (auto id : cfg.android_power_config().battery_counters()) {
    PERFETTO_CHECK(id < counters_enabled_.size());
    counters_enabled_.set(id);
  }
}

AndroidPowerDataSource::~AndroidPowerDataSource() = default;

void AndroidPowerDataSource::Start() {
  Tick();
}

void AndroidPowerDataSource::Tick() {
  // Post next task.
  auto now_ms = base::GetWallTimeMs().count();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this] {
        if (weak_this)
          weak_this->Tick();
      },
      poll_rate_ms_ - (now_ms % poll_rate_ms_));

  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  auto* counters_proto = packet->set_battery();

  for (size_t i = 0; i < counters_enabled_.size(); i++) {
    if (!counters_enabled_.test(i))
      continue;
    auto counter = static_cast<android_hal::BatteryCounter>(i);
    auto value = GetCounter(counter);
    if (value.has_value())
      continue;
    switch (counter) {
      case android_hal::BatteryCounter::kCharge:
        counters_proto->set_charge_counter_uah(*value);
        break;

      case android_hal::BatteryCounter::kCapacityPercent:
        counters_proto->set_capacity_percent(static_cast<int>(*value));
        break;

      case android_hal::BatteryCounter::kCurrent:
        counters_proto->set_current_ua(*value);
        break;
      case android_hal::BatteryCounter::kCurrentAvg:
        counters_proto->set_current_avg_ua(*value);
        break;
    }
  }
}

void AndroidPowerDataSource::Flush(FlushRequestID,
                                   std::function<void()> callback) {
  writer_->Flush(callback);
}

}  // namespace perfetto
