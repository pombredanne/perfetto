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

#include "src/android_hal/health_hal.h"

#include <android/hardware/health/2.0/IHealth.h>
#include <healthhalutils/HealthHalUtils.h>

namespace perfetto {
namespace android_hal {

using ::android::hardware::health::V2_0::IHealth;
using ::android::hardware::health::V2_0::Result;

namespace {

android::sp<IHealth> g_svc;

void ResetService() {
  g_svc = ::android::hardware::health::V2_0::get_health_service();
}

}  // namespace

bool GetBatteryCounter(BatteryCounter counter, int64_t* value) {
  *value = 0;
  if (!g_svc)
    ResetService();

  if (!g_svc)
    return false;

  bool res = false;
  switch (counter) {
    case BatteryCounter::kCharge:
      g_svc->getChargeCounter([&res, value](auto hal_res, int32_t hal_value) {
        res = hal_res == Result::SUCCESS;
        *value = hal_value;
      });
      break;

    case BatteryCounter::kCapacityPercent:
      g_svc->getCapacity([&res, value](auto hal_res, int64_t hal_value) {
        res = hal_res == Result::SUCCESS;
        *value = hal_value;
      });
      break;

    case BatteryCounter::kCurrent:
      g_svc->getCurrentNow([&res, value](auto hal_res, int32_t hal_value) {
        res = hal_res == Result::SUCCESS;
        *value = hal_value;
      });
      break;

    case BatteryCounter::kCurrentAvg:
      g_svc->getCurrentAverage([&res, value](auto hal_res, int32_t hal_value) {
        res = hal_res == Result::SUCCESS;
        *value = hal_value;
      });
      break;
  }
  return res;
}

}  // namespace android_hal
}  // namespace perfetto
