#!/usr/bin/python
# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import os

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('INPUT')
  parser.add_argument('OUTPUT')
  args = parser.parse_args()

  with open(args.INPUT, 'rb') as f:
    s = f.read()

  root, name = os.path.split(args.OUTPUT)

  h_path = os.path.join(root, name + '.gen.h')

  constant_name = 'k' + name.title().replace('_', '')
  binary = ', '.join((hex(ord(c)) for c in s))

  with open(h_path, 'wb') as f:
    f.write("""
#include <stdint.h>
#include <stddef.h>

#include <array>

namespace perfetto {{

const std::array<uint8_t, {size}> {constant_name}{{{{{binary}}}}};

}} // namespace perfetto
""".format(**{
      'size': len(s),
      'constant_name': constant_name,
      'binary': binary,
    }))

if __name__ == '__main__':
  exit(main())
