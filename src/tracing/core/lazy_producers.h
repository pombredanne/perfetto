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

#ifndef SRC_TRACING_CORE_LAZY_PRODUCERS_H_
#define SRC_TRACING_CORE_LAZY_PRODUCERS_H_

#include <map>
#include <string>

namespace perfetto {

class LazyProducers {
 public:
  class Handle {
   public:
    friend class LazyProducers;
    friend void swap(Handle&, Handle&);

    Handle() = default;
    ~Handle();

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle) = delete;

    Handle(Handle&&) noexcept;
    Handle& operator=(Handle&&) noexcept;

   private:
    Handle(LazyProducers* lazy_producers, std::string property_name);

    LazyProducers* lazy_producers_ = nullptr;
    std::string property_name_;
  };

  friend class Handle;

  Handle EnableProducer(const std::string& name);
  virtual ~LazyProducers();

 protected:
  // virtual for testing.
  virtual bool SetAndroidProperty(const std::string& name,
                                  const std::string& value);
  virtual std::string GetAndroidProperty(const std::string& name);

 private:
  void DecrementPropertyRefCount(const std::string& property);
  std::map<std::string, size_t> system_property_refcounts_;
};

void swap(LazyProducers::Handle&, LazyProducers::Handle&);

}  // namespace perfetto

#endif  // SRC_TRACING_CORE_LAZY_PRODUCERS_H_
