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

#ifndef SRC_ANDROID_BINDER_HEALTH_HAL_H_
#define SRC_ANDROID_BINDER_HEALTH_HAL_H_

#include <stddef.h>
#include <stdint.h>

// This header declares proxy functions defined in libperfetto_binder.so that
// allow traced_probes to access internal android functions via hwbinder.
// Do not add any include to either perfetto headers or android headers. See
// README.md for more.

namespace perfetto {
namespace android_binder {

enum class BatteryCounter {
  kUnspecified = 0,
  kCharge,
  kCapacityPercent,
  kCurrent,
  kCurrentAvg,
};

extern "C" {

bool __attribute__((visibility("default")))
GetBatteryCounter(BatteryCounter, int64_t*);

}  // extern "C"

}  // namespace android_binder
}  // namespace perfetto

#endif  // SRC_ANDROID_BINDER_HEALTH_HAL_H_
