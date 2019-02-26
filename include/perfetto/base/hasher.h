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

#ifndef INCLUDE_PERFETTO_BASE_HASHER_H_
#define INCLUDE_PERFETTO_BASE_HASHER_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace perfetto {
namespace base {

// A helper class which computes a 64-bit hash of the input data.
// The algorithm used is FNV-1a as it is fast and easy to implement and has
// relatively few collisions.
class Hasher {
 public:
  // Creates an empty hasher object
  Hasher() {}

  // Hashes a 64 bit double.
  void Hash(double data) {
    uint64_t reintr = 0;
    static_assert(sizeof(data) == sizeof(reintr),
                  "double's size does not match uint64_t");
    memcpy(&reintr, &data, sizeof(data));
    Hash(reintr);
  }

  // Hashes a 64 bit unsigned integer.
  void Hash(uint64_t data) {
    for (size_t i = 0; i < 8; i++) {
      result_ *= kFnv1a64Prime;
      result_ ^= static_cast<uint8_t>(data >> (i * 8) & 0xff);
    }
  }

  // Hashes a string.
  void Hash(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
      result_ *= kFnv1a64Prime;
      result_ ^= static_cast<uint8_t>(data[i]);
    }
  }

  uint64_t result() { return result_; }

 private:
  static constexpr uint64_t kFnv1a64OffsetBasis = 0xcbf29ce484222325;
  static constexpr uint64_t kFnv1a64Prime = 0x100000001b3;

  uint64_t result_ = kFnv1a64OffsetBasis;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_HASHER_H_
