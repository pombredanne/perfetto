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

import argparse
import difflib
import glob
import os
import subprocess
import sys

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_DATA_DIR = os.path.join(ROOT_DIR, "test", "data")

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--traces', type=str, help='location of trace files',
                      default=os.path.join(TEST_DATA_DIR, "traces"))
  parser.add_argument('--queries', type=str, help='location of query files',
                      default=os.path.join(TEST_DATA_DIR, "queries"))
  parser.add_argument('--expected', type=str, help='location of expected files',
                      default=os.path.join(TEST_DATA_DIR, "expected"))
  parser.add_argument('trace_processor', type=str,
                      help='location of trace processor binary')
  args = parser.parse_args()

  expected_files = glob.glob(os.path.join(args.expected, "*.out"))

  for f in expected_files:
    filename_with_ext = os.path.basename(f)
    (filename, _) = os.path.splitext(filename_with_ext)

    [trace_name, query_name] = filename.split('-')

    trace_path = os.path.join(args.traces, trace_name + '.pb')
    query_path = os.path.join(args.queries, query_name + '.sql')

    if not os.path.exists(trace_path) or not os.path.exists(query_path):
      print("Trace or query file not found for expected file {}".format(f))
      return 1

    with open(query_path, "r") as query_file:
      actual_raw = subprocess.check_output([args.trace_processor, trace_path],
                                           stdin=query_file, stderr=None)
      actual = actual_raw.decode("utf-8")

    with open(f, "r") as expected_file:
      expected = expected_file.read()
      if expected != actual:
        print("Expected did not match actual for {}".format(f))

  return 0

if __name__ == '__main__':
  sys.exit(main())
