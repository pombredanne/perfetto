# Perfetto benchmarks

TODO(primiano): summarize results of `perfetto_benchmarks` (repro steps or
didn't happen)

**TL;DR:**  
Peak producer-to-service tracing bandwidth (using 64 KB chunk sizes):
* Linux desktop: ~1.3 GB/s
* Android Pixel: ~1 GB/s

Producer-to-service CPU overhead when writing ~3 MB/s: 0.01 - 0.03
(0.01 := 1% cpu time of one core)

CPU overhead for translating ftrace raw pipe into protobuf:
* Android Pixel: 0.00-0.01 when idle.
* Android Pixel: 0.02-0.04 with 8 cores @ 8.0 CPU usage (raytracer).
* Linux desktop: TBD
