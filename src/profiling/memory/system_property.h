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

#ifndef SRC_PROFILING_MEMORY_SYSTEM_PROPERTY_H_
#define SRC_PROFILING_MEMORY_SYSTEM_PROPERTY_H_

#include <map>
#include <string>

namespace perfetto {
namespace profiling {

class SystemProperties {
 public:
  class Handle {
   public:
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&&);
    Handle& operator=(Handle&&);

    friend class SystemProperties;
    ~Handle();
    operator bool();

   private:
    explicit Handle(SystemProperties* system_properties, std::string property);
    explicit Handle(SystemProperties* system_properties);

    SystemProperties* system_properties_;
    std::string property_;
    bool all_ = false;
  };

  Handle SetProperty(std::string name);
  Handle SetAll();

  virtual bool SetAndroidProperty(const std::string& name,
                                  const std::string& value);

  virtual ~SystemProperties();

 protected:
 private:
  void UnsetProperty(const std::string& name);
  void UnsetAll();

  size_t alls_ = 0;
  std::map<std::string, size_t> properties_;
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_SYSTEM_PROPERTY_H_
