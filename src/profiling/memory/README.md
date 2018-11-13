# heapprofd - Android Heap Profiler

_These are temporary instructions while heapprofd is under development. They are
subject to frequent change and will be obsoleted once heapprofd is integrated
into Perfetto._

Currently heapprofd only works with SELinux disabled and when run as root.

To start profiling the process `${PID}`, run the following sequence of commands.
Adjust the `INTERVAL` to trade-off runtime impact for higher accuracy of the
results. If `INTERVAL=1`, every allocation is sampled for maximum accuracy.
Otherwise, a sample is taken every `INTERVAL` bytes on average.

```bash
INTERVAL=128000

adb shell su root setenforce 0
adb shell su root start heapprofd

echo '
buffers {
  size_kb: 100024
  fill_policy: RING_BUFFER
}

data_sources {
  config {
    name: "android.heapprofd"
    target_buffer: 0
    heapprofd_config {
      sampling_interval_bytes: '${INTERVAL}'
      pid: '${PID}'
      continuous_dump_config {
        dump_phase_ms: 10000
        dump_interval_ms: 1000
      }
    }
  }
}

duration_ms: 20000
' | adb shell perfetto -t -c - -o /data/misc/perfetto-traces/trace

adb pull /data/misc/perfetto-traces/trace /tmp/trace
```

To obtain heap dumps for all profiled processes, send `SIGUSR1` to heapprofd
which produces heap dumps in /data/local/tmp.

```bash
adb shell killall -USR1 heapprofd
adb pull /data/local/tmp/heap_dump.${PID}
```

This file can then be converted to a flamegraph using Brendan Gregg's
[`flamegraph.pl`](
  https://github.com/brendangregg/FlameGraph/blob/master/flamegraph.pl).

```bash
flamegraph.pl heap_dump.${PID} > heap_dump.${PID}.svg
```
