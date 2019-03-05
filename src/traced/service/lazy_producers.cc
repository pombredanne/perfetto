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

#include "src/traced/service/lazy_producers.h"

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/system_properties.h>
#endif

namespace perfetto {
namespace {

const std::map<std::string, std::string>& ProducerToProperty() {
  static std::map<std::string, std::string>* producer_to_property =
      new std::map<std::string, std::string>(
          {{"android.heapprofd", "persist.heapprofd.enable"}});
  return *producer_to_property;
}

}  // namespace

void LazyProducers::StartTracing(TracingSessionID tsid,
                                 const TraceConfig& cfg) {
  std::set<std::string> properties;

  for (const TraceConfig::DataSource& ds : cfg.data_sources()) {
    const std::string& name = ds.config().name();
    auto it = ProducerToProperty().find(name);
    if (it == ProducerToProperty().end())
      continue;

    if (system_property_refcounts_[it->second]++ == 0) {
      // Do not mess if the property was already explicitely set.
      std::string prev_value = GetAndroidProperty(it->second);
      if (prev_value != "" && prev_value != "0")
        continue;
      // We failed to set the property. Don't do anything.
      if (!SetAndroidProperty(it->second, "1"))
        continue;
    }

    properties.emplace(it->second);
  }

  properties_for_trace_config_.emplace(tsid, std::move(properties));
}
void LazyProducers::StopTracing(TracingSessionID tsid) {
  auto it = properties_for_trace_config_.find(tsid);
  if (it == properties_for_trace_config_.end())
    return;

  for (const std::string& property : it->second)
    DecrementPropertyRefCount(property);
}

void LazyProducers::DecrementPropertyRefCount(
    const std::string& property_name) {
  auto it = system_property_refcounts_.find(property_name);
  if (it == system_property_refcounts_.end()) {
    PERFETTO_DFATAL("This should not happen");
    return;
  }

  if (--it->second == 0) {
    system_property_refcounts_.erase(it);
    SetAndroidProperty(property_name, "0");
  }
}

bool LazyProducers::SetAndroidProperty(const std::string& name,
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

std::string LazyProducers::GetAndroidProperty(const std::string& name) {
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

LazyProducers::~LazyProducers() {
  PERFETTO_DCHECK(system_property_refcounts_.empty());
}

}  // namespace perfetto
