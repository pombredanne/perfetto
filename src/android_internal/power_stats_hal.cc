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

#include "src/android_internal/power_stats_hal.h"

#include <algorithm>
#include <map>
#include <string.h>

#include <android/hardware/power/stats/1.0/IPowerStats.h>

namespace perfetto {
namespace android_internal {

using android::hardware::hidl_vec;
using android::hardware::Return;
using android::hardware::power::stats::V1_0::EnergyData;
using android::hardware::power::stats::V1_0::IPowerStats;
using android::hardware::power::stats::V1_0::RailInfo;
using android::hardware::power::stats::V1_0::Status;

namespace {

android::sp<IPowerStats> g_svc;
std::map<uint32_t, RailInfo>* g_rail_info = nullptr;

bool GetService() {
  if(!g_svc)
    g_svc = IPowerStats::getService();

  return g_svc != nullptr;
}

bool RetrieveRailInfo() {
  if(g_rail_info) {
    return true;
  }

  Status status;
  hidl_vec<RailInfo> rails;
  auto rails_cb =
      [&rails, &status](hidl_vec<RailInfo> r, Status s) {
        rails = r;
        status = s;
      };

  Return<void> ret = g_svc->getRailInfo(rails_cb);
  if (status == Status::SUCCESS) {
    g_rail_info = new std::map<uint32_t, RailInfo>();
    for (int i = 0; i < rails.size(); ++i) {
      (*g_rail_info)[rails[i].index] = rails[i];
    }
    return true;
  }

  return false;
}

} // namespace

bool GetNumberOfRails(uint32_t* num_rails) {
  *num_rails = 0;
  if (!GetService() || !RetrieveRailInfo())
    return false;

  *num_rails = g_rail_info->size();
  return true;
}

bool GetRailEnergyData(RailEnergyData* rail_array, size_t* rail_array_size) {
  const size_t in_rail_array_size = *rail_array_size;
  *rail_array_size = 0;

  if (!GetService() || !RetrieveRailInfo())
    return false;

  if (g_rail_info->empty())
    return true;  // This device has no power rails

  Status status;
  hidl_vec<EnergyData> measurements;
  auto energy_cb =
      [&measurements, &status](hidl_vec<EnergyData> m, Status s) {
        measurements = m;
        status = s;
      };

  Return<void> ret = g_svc->getEnergyData(hidl_vec<uint32_t>(), energy_cb);
  if (status != Status::SUCCESS) {
    return false;
  }

  *rail_array_size = std::min(in_rail_array_size, measurements.size());
  for (int i = 0; i < *rail_array_size; ++i) {
    const RailInfo& railInfo = (*g_rail_info)[measurements[i].index];
    RailEnergyData& element = rail_array[i];

    strncpy(element.rail_name, railInfo.railName.c_str(), sizeof(element.rail_name));
    strncpy(element.subsys_name, railInfo.subsysName.c_str(), sizeof(element.subsys_name));
    element.rail_name[sizeof(element.rail_name) - 1] = '\0';
    element.subsys_name[sizeof(element.subsys_name) - 1] = '\0';

    element.timestamp = measurements[i].timestamp;
    element.energy = measurements[i].energy;
  }

  return true;

}

}  // namespace android_internal
}  // namespace perfetto
