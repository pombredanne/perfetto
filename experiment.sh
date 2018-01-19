#!/bin/bash

set -e

OUT_DIR=out/linux_clang_debug/
CATAPULT_PATH=$HOME/proj/catapult/

RUN_METRIC_PATH=$CATAPULT_PATH/tracing/bin/run_metric



DURATION_MS="${DURATION_MS:-12000}"
BUFFER_SIZE_KB="${BUFFER_SIZE_KB:-}"
DRAIN_PERIOD_MS="${DRAIN_PERIOD_MS:-}"

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

if [[ -z "$DURATION_MS" ]]; then
  echo "Please set DURATION_MS"
  exit 1
fi

if [[ -z "$BUFFER_SIZE_KB" ]]; then
  echo "Please set BUFFER_SIZE_KB"
  exit 1
fi

if [[ -z "$DRAIN_PERIOD_MS" ]]; then
  echo "Please set DRAIN_PERIOD_MS"
  exit 1
fi

if [[ -z "$1" ]]; then
  echo "Usage: $0 output_dir"
  exit 1
fi

output=$1

adb shell setenforce 0

tmpdir=$(mktemp -d)
trap 'rm -rf -- "$tmpdir"' INT TERM HUP EXIT

mkdir $output

make_config $BUFFER_SIZE_KB $DRAIN_PERIOD_MS $DURATION_MS> $tmpdir/experiment.cfg.protobuf
adb push $tmpdir/experiment.cfg.protobuf /data/local/

set -x

adb shell mkdir -p /sys/kernel/debug/tracing/instances/meta

adb shell stop traced
adb shell stop traced_probes

adb shell traced &
adb shell traced_probes &

adb shell perfetto -c /data/local/experiment.cfg.protobuf -o /data/local/out.protobuf &
./buildtools/android_sdk/platform-tools/systrace/systrace.py -t 10 -o $output/trace.html sched

set +x

for i in {0..7}; do
  adb pull /sys/kernel/debug/tracing/instances/meta/per_cpu/cpu$i/stats $output/stats$i
done

find $output/ -type f | grep stats | xargs grep '^overrun:' | cut -d' ' -f2 | paste -sd+ - | bc > $output/overrun

$RUN_METRIC_PATH $output/trace.html androidSystraceMetric --also-output-json --filename=$output/results
cat $output/results.json | jq 'map(select(.name | contains("traced"))) | map({ name: .name, value: .sampleValues[0]})' > $output/traced_results.json

echo 'dropped:' $(cat $output/overrun)

