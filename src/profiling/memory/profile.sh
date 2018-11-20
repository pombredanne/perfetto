#!/bin/bash

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

set -euo pipefail

while getopts ":i:d:p:n:" opt; do
  case $opt in
    i) INTERVAL=$OPTARG;;
    d) DURATION=$OPTARG;;
    p) PID=$OPTARG;;
    n) NAME=$OPTARG;;
    ?) echo "Usage: profile.sh -i INTERVAL -d DURATION [-p PID] [-n NAME]"
       exit;;
  esac
done

if prodcertstatus > /dev/null; then
  # If we have prodaccess, use the binfs trace_to_text version.
  DIR=/google/bin/users/fmayer/third_party/perfetto:trace_to_text_sig
else
  # If the user uses a bundle on their workstation, find trace_to_text that
  # co-locates this binary.
  DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"
fi;

ENFORCE_MODE=$(adb shell getenforce)
function finish {
  adb shell su root stop heapprofd
  adb shell su root setenforce $ENFORCE_MODE
}
trap finish EXIT

adb shell su root setenforce 0
adb shell su root start heapprofd

CFG='
buffers {
  size_kb: 100024
}

data_sources {
  config {
    name: "android.heapprofd"
    target_buffer: 0
    heapprofd_config {
      sampling_interval_bytes: '${INTERVAL}'
      '${PID+"pid: $PID"}'
      '${NAME+"process_cmdline: \"$NAME\""}'
      continuous_dump_config {
        dump_phase_ms: 10000
        dump_interval_ms: 1000
      }
    }
  }
}

duration_ms:'$DURATION'
'

PERFETTO_PID=$(adb exec-out 'CFG='"'${CFG}'
"'echo ${CFG} | perfetto -t -c - -o /data/misc/perfetto-traces/trace -b'\
  | grep -o '^pid: [0-9]*$' | awk '{ print $2 }')

adb exec-out "while [[ -d /proc/$PERFETTO_PID ]]; do sleep 1; done" | cat
adb pull /data/misc/perfetto-traces/trace /tmp/trace > /dev/null

OUTDIR=$($DIR/trace_to_text profile /tmp/trace | grep -o "/tmp/[^ ]*")
gzip $OUTDIR/*.pb

echo "Wrote profiles to $OUTDIR"
