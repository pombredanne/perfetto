/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/traced/service/lazy_producer.h"

#include "perfetto/base/build_config.h"

#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/system_properties.h>
#endif

namespace perfetto {

LazyProducer::LazyProducer(base::TaskRunner* task_runner,
                           uint32_t delay_ms,
                           std::string data_source_name,
                           std::string property_name)
    : task_runner_(task_runner),
      delay_ms_(delay_ms),
      data_source_name_(data_source_name),
      property_name_(property_name),
      weak_factory_(this) {}

void LazyProducer::ConnectInProcess(TracingService* svc) {
  endpoint_ = svc->ConnectProducer(this, geteuid(), "lazy_producer",
                                   /*shm_hint_kb*/ 16);
}

void LazyProducer::OnConnect() {
  DataSourceDescriptor dsd;
  dsd.set_name(data_source_name_);
  endpoint_->RegisterDataSource(dsd);
}

void LazyProducer::SetupDataSource(DataSourceInstanceID id,
                                   const DataSourceConfig&) {
  if (active_sessions_.empty()) {
    std::string prev_value = GetAndroidProperty(property_name_);
    if (prev_value != "2")
      if (!SetAndroidProperty(property_name_, "1"))
        return;
  }
  active_sessions_.emplace(id);
  generation_++;
}

void LazyProducer::StopDataSource(DataSourceInstanceID id) {
  if (!active_sessions_.erase(id))
    return;

  if (!active_sessions_.empty())
    return;

  uint64_t cur_generation = generation_;
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, cur_generation] {
        if (!weak_this)
          return;
        std::string prev_value =
            weak_this->GetAndroidProperty(weak_this->property_name_);
        if (prev_value != "2" && weak_this->generation_ == cur_generation)
          weak_this->SetAndroidProperty(weak_this->property_name_, "0");
      },
      delay_ms_);
}

bool LazyProducer::SetAndroidProperty(const std::string& name,
                                      const std::string& value) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  return __system_property_set(name.c_str(), value.c_str()) == 0;
#else
  // Allow this to be mocked out for tests on other platforms.
  base::ignore_result(name);
  base::ignore_result(value);
  PERFETTO_FATAL("Properties can only be set on Android.");
#endif
}

std::string LazyProducer::GetAndroidProperty(const std::string& name) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // TODO(fmayer): Use __system_property_read_callback once we target a newer
  // API level
  char value[PROP_NAME_MAX];
  __system_property_get(name.c_str(), value);
  value[PROP_NAME_MAX - 1] = '\0';
  return value;
#else
  // Allow this to be mocked out for tests on other platforms.
  base::ignore_result(name);
  PERFETTO_FATAL("Properties can only be read on Android.");
#endif
}

LazyProducer::~LazyProducer() {
  if (!active_sessions_.empty())
    SetAndroidProperty(property_name_, "0");
}

}  // namespace perfetto
