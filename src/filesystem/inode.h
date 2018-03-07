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

#ifndef SRC_FILESYSTEM_INODE_H_
#define SRC_FILESYSTEM_INODE_H_

#include <sys/stat.h>
#include <map>
#include <string>
#include <vector>

namespace perfetto {

// On ARM, st_dev is not dev_t but unsigned long long.
using block_device_t = decltype(stat::st_dev);

std::map<block_device_t, std::vector<std::string>> ParseMounts();
}  // namespace perfetto

#endif  // SRC_FILESYSTEM_INODE_H_
