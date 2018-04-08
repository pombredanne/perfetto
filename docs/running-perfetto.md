# Running Perfetto

In order to run Perfetto and get a meaningful trace you need to build
(see [build instructions](build-instructions.md)) and run the following:

`traced`: The unprivileged trace daemon that owns the log buffers and maintains
a registry of Producers and Consumers connected.

`traced_probes`: The privileged daemon that has access to the Kernel tracefs
(typically mounted under `/sys/kernel/debug/tracing`). It drives
[Ftrace](https://source.android.com/devices/tech/debug/ftrace) and writes its
protobuf-translated contents into `traced`.

`perfetto`: A command line utility client that drive the trace and save back
the results (either to a file or to [Android's Dropbox][dropbox])

Running from a standalone checkout (Linux, Mac or Android)
-------------------------------------------------------------
A convenience script allows to run Perfetto daemons (`traced`, `traced_probes`)
and the command line client (`perfetto`) in a tmux-based terminal:
```
CONFIG=ftrace.cfg OUT=out/default ./tools/tmux
```

The script will automatically serialize the trace config defined in the
`CONFIG` variable (e.g., [this](https://android.googlesource.com/platform/external/perfetto/+/master/test/configs/ftrace.cfg)) into a protobuf and setup the right paths.
Furthermore it will automatically rebuild if necessary.

Running from an Android P+ in-tree build
----------------------------------------
Make sure that Perfetto daemons (`traced` / `traced_probes`) are running.
They are enabled by default on Pixel and Pixel 2 (walleye, taimen, marlin,
sailfish). On other devices start them manually by doing:
```
adb shell setprop persist.traced.enable 1
```

If this works you will see something like this in the logs:
```
$ adb logcat -s perfetto
perfetto: service.cc:45 Started traced, listening on /dev/socket/traced_producer /dev/socket/traced_consumer
perfetto: probes.cc:25 Starting /system/bin/traced_probes service
perfetto: probes_producer.cc:32 Connected to the service
```

At which point you can grab a trace by doing:

```
$ adb shell perfetto --config :test --out /data/misc/perfetto-traces/trace
```

For more advanced configurations see the [Trace Config](#trace-config) section.

Note: If the output file is not under `/data/misc/perfetto-traces`, tracing will
fail due to SELinux.


Trace config
------------
`--config :test` uses a hard-coded test trace config. It is possible to pass
an arbitrary trace config by doing the following:
```
cat > /tmp/config.txpb <<EOF
# This is a text-encoded protobuf for /protos/perfetto/config/trace_config.proto
duration_ms: 10000

# For long traces set the following variables. It will periodically drain the
# trace buffers into the output file, allowing to save a trace larger than the
# buffer size.
write_into_file: true
file_write_period_ms: 5000

buffers {
  size_kb: 10240
}

data_sources {
  config {
    name: "linux.ftrace"
    target_buffer: 0
    ftrace_config {
      buffer_size_kb: 40 # Kernel ftrace buffer size.
      ftrace_events: "sched_switch"
      ftrace_events: "print"
    }
  }
}

data_sources {
  config {
    name: "linux.process_stats"
    target_buffer: 0
  }
}
EOF

protoc=$(pwd)/out/android/gcc_like_host/protoc

$protoc --encode=perfetto.protos.TraceConfig \
        -I$(pwd)/external/perfetto/protos \
        $(pwd)/external/perfetto/protos/perfetto/config/perfetto_config.proto \
        < /tmp/config.txpb \
        > /tmp/config.pb

cat /tmp/config.pb | adb shell perfetto -c - -o /data/misc/perfetto-traces/trace.pb
adb pull /data/misc/perfetto-traces/trace.pb /tmp/
out/android/trace_to_text json < /tmp/trace.pb > /tmp/trace.json

# The file can now be viewed in chrome://tracing
```


[dropbox]: https://developer.android.com/reference/android/os/DropBoxManager.html
