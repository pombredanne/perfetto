#!/bin/bash

set -e

OUT_DIR=out/linux_clang_debug/

function make_config() {
  local buffer_size_kb=$1
  local drain_period_ms=$2
  local duration_ms=$3

  cat test/configs/template.cfg |
  sed "s/ZZZ_BUFFER_SIZE_KB/$buffer_size_kb/" |
  sed "s/ZZZ_DRAIN_PERIOD_MS/$drain_period_ms/" |
  sed "s/ZZZ_DURATION_MS/$duration_ms/" |
  $OUT_DIR/protoc_helper encode
}

make_config 1024 100 5000 > experiment.cfg.protobuf
adb push experiment.cfg.protobuf /data/local/

set -x

adb shell mkdir -p /sys/kernel/debug/tracing/instances/meta

adb shell stop traced
adb shell stop traced_probes

adb shell start traced
adb shell start traced_probes

#adb shell perfetto -c /data/local/experiment.cfg.protobuf -o /data/local/out.protobuf &
./buildtools/android_sdk/platform-tools/systrace/systrace.py -t 30 -o out.html sched 

adb shell stop traced
adb shell stop traced_probes

