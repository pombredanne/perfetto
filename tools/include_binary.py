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

  h_path = os.path.join(root, name + '.h')
  cc_path = os.path.join(root, name + '.cc')

  include_guard = h_path.replace('/', '_').replace('.', '_').upper() + '_'
  constant_name = 'k' + name.title().replace('_', '')
  binary = ', '.join((hex(ord(c)) for c in s))

  with open(h_path, 'wb') as f:
    f.write("""
#ifndef {include_guard}
#define {include_guard}

#include <stdint.h>
#include <stddef.h>

namespace perfetto {{

extern const size_t {constant_name}Size;
extern const uint8_t {constant_name}[{size}];

}} // namespace perfetto

#endif  // {include_guard}
""".format(**{
      'size': len(s),
      'include_guard': include_guard,
      'constant_name': constant_name,
    }))

  with open(cc_path, 'wb') as f:
    f.write("""
#include "{h_path}"

namespace perfetto {{

const size_t {constant_name}Size = {size};
const uint8_t {constant_name}[{size}] = {{
{binary}
}};

}} // namespace perfetto
""".format(**{
      'h_path': name + '.h',
      'size': len(s),
      'binary': binary,
      'constant_name': constant_name,
    }))

if __name__ == '__main__':
  exit(main())
