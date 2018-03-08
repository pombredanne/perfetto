#!/usr/bin/env python
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

# Get transitive closure of affected files considering header dependencies.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import os
import re
import sys
import subprocess

from collections import defaultdict

HEADER_RE = re.compile(r'#include "(.*)"')
HEADER_DIRS = ['include', 'src']


def GetIncludes(filename):
  if not filename.endswith('.h') and not filename.endswith('.cc'):
    return []
  directory = os.path.dirname(filename)
  incs = []
  with open(filename) as fd:
    for inc in HEADER_RE.findall(fd.read()):
      if '/' not in inc:
        inc = os.path.join(directory, inc)
      incs.append(inc)
  return incs


def GetIncludedFiles(includes):
  incs = []
  for inc in includes:
    for header_dir in HEADER_DIRS:
      incs.append(os.path.join(header_dir, inc))
  return incs


def BuildHeaderDatabase(root):
  d = defaultdict(set)
  for directory, dirs, files in os.walk(root):
    for filename in files:
      fully_qualified = os.path.join(directory, filename)
      for fn in GetIncludedFiles(GetIncludes(fully_qualified)):
        d[fn].add(fully_qualified)
  return d


def GetUpstream():
  p = subprocess.Popen(
      ['git', 'rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{u}'],
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE)
  upstream, err = p.communicate()
  if p.returncode:
    if 'no upstream configured' not in err:
      print('No upstream found', file=sys.stderr)
      exit(1)
    else:
      upstream = 'origin/master'
  return upstream


def GetChangedFiles(upstream):
  return set(
      line for line in subprocess.check_output(
          ['git', 'diff', '--name-only', upstream]).split('\n') if line)


def UpdateHeaderDatabase(old, new):
  for key, value in new.iteritems():
    old[key] |= value


if __name__ == '__main__':
  changed = set()
  newchanged = GetChangedFiles(GetUpstream())
  hdr_db = BuildHeaderDatabase('src')
  UpdateHeaderDatabase(hdr_db, BuildHeaderDatabase('include'))

  while newchanged - changed:
    changed |= newchanged
    newchanged = set()
    for filename in changed:
      if filename.endswith('.h'):
        newchanged |= hdr_db[filename]

  print('\n'.join(changed))
