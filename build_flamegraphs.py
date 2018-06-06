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

import json
import subprocess
import sys

# To prevent JSON parsing from crashing.
sys.setrecursionlimit(10000000)

def prt(elem, parent_names, name=None):
  if name is None:
    name = '`'.join(elem.get('name', '(unknown)').split('`')[::-1])
  new_parent_names = parent_names + [name]
  cs = sum(x['value'] for x in elem.get('children', []))
  yield ';'.join(new_parent_names) + ' ' + str(elem['value'] - cs) + '\n'
  for c in elem.get('children', []):
    for line in prt(c, new_parent_names, None):
      yield line

names = {}
with open('pse') as fd:
  for line in fd:
    if line.startswith('USER'):
      continue
    recs = line.strip().split()
    names[recs[1]] = recs[8]

with open('heapd') as fd:
  j = json.load(fd)

for pid, value in j.iteritems():
  if len(sys.argv) > 1:
    if pid not in sys.argv[1:]:
      continue
  with open('out/graph{}.svg'.format(pid), 'w') as outfd:
    p = subprocess.Popen(['/usr/local/google/home/fmayer/tmphttp/flamegraph.pl'], stdin=subprocess.PIPE, stdout=outfd)
    for line in prt(j[pid][0], [], names.get(pid, '(unknown)')):
      p.stdin.write(line)
