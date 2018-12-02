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

// Fallback implementation used in standalone builds, where we cannot depend
// on Android tree internals and hence the IHealth service.

namespace perfetto {
namespace android_hal {

bool GetBatteryCounter(BatteryCounter, int64_t* value) {
  *value = 0;
  return false;
}

}  // namespace android_hal
}  // namespace perfetto
