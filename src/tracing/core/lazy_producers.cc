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

#include "src/tracing/core/lazy_producers.h"

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

LazyProducers::Handle::~Handle() {
  if (lazy_producers_)
    lazy_producers_->DecrementPropertyRefCount(property_name_);
}

LazyProducers::Handle::Handle(Handle&& other) noexcept {
  Handle tmp(std::move(other));
  using std::swap;
  swap(*this, tmp);
}

LazyProducers::Handle& LazyProducers::Handle::operator=(
    Handle&& other) noexcept {
  Handle tmp(std::move(other));
  using std::swap;
  swap(*this, tmp);
  return *this;
}

LazyProducers::Handle::Handle(LazyProducers* lazy_producers,
                              std::string property_name)
    : lazy_producers_(lazy_producers),
      property_name_(std::move(property_name)) {}

LazyProducers::Handle LazyProducers::EnableProducer(const std::string& name) {
  auto it = ProducerToProperty().find(name);
  if (it == ProducerToProperty().end())
    return Handle(nullptr, "");
  if (system_property_refcounts_[it->second]++ == 0) {
    if (GetAndroidProperty(it->second) != "")
      return Handle(nullptr, "");
    if (!SetAndroidProperty(it->second, "1"))
      return Handle(nullptr, "");
  }
  return Handle(this, it->second);
}

void LazyProducers::DecrementPropertyRefCount(
    const std::string& property_name) {
  auto it = system_property_refcounts_.find(property_name);
  if (it == system_property_refcounts_.end()) {
    PERFETTO_DCHECK("This should not happen");
    return;
  }

  if (--it->second == 0) {
    system_property_refcounts_.erase(it);
    SetAndroidProperty(property_name, "");
  }
}

void swap(LazyProducers::Handle& a, LazyProducers::Handle& b) {
  using std::swap;
  swap(a.lazy_producers_, b.lazy_producers_);
  swap(a.property_name_, b.property_name_);
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
  std::string value;
  const prop_info* pi = __system_property_find(name);
  if (pi) {
    __system_property_read_callback(
        pi,
        [](void* cookie, const char*, const char* value, uint32_t) {
          *reinterpret_cast<std::string*>(cookie) = value;
        },
        &value);
  }
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
