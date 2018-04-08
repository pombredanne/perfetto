# Perfetto - Performance instrumentation and tracing

Perfetto is an open-source project for performance instrumentation and tracing
Linux/Android platforms and user-space apps. It consists of:
- A portable, high efficiency, user-space tracing library, designed for tracing
  of multi-process systems, based on zero-alloc zero-copy zero-syscall
  (on fast-paths) writing of protobufs over shared memory.
- A library for converting Linux/Android kernel [Ftrace][ftrace] events into
  API-stable tracing protobufs on-device with low overhead.
- Linux/Android daemons for centralized coordination of tracing.
- OS-wide Linux/Android probes for platform debugging, beyond ftrace: e.g.,
  I/O tracing, heap profiling (coming soon), perf sampling (coming soon).
- Web-based UI for inspection and analysis of traces (coming soon)

![Perfetto Stack](docs/images/perfetto-stack.png)

Goals
-----
Perfetto is building the next-gen unified tracing ecosystem for:
- Android platform tracing ([Systrace][systrace])
- Chrome platform tracing ([chrome://tracing][chrome-tracing])
- App-defined user-space tracing (including support for non-Android apps).

The goal is to create an open, portable and developer friendly tracing ecosystem
for app and platform performance debugging.

Key features
------------
**Designed for production**
Perfetto's tracing library and daemons are designed for use in production.
Privilege isolation is a key design goal:
* The interface for writing trace events are decoupled from the interface for
  read-back and control and can be subjected to different ACLs.
* Despite being based on shared memory, Perfetto is designed to prevent
  cross-talk between data sources, even in case of arbitrary code execution
  (memory is shared point-to-point, memory is never shared between processes).
* Perfetto daemons are designed following to the principle of least privilege,
  in order to allow strong sandboxing (via SELinux on Android).

See [docs/security-model.md](docs/security-model.md) for more details.

**Long traces**
Pefetto aims at supporting hours-long / O(100GB) traces, both in terms of
recording backend and UI frontend.

**Interoperability**
Perfetto traces (output) and configuration (input) consists of protobuf
messages, in order to allow interoperability with several languages.

See [docs/trace-format.md](docs/trace-format.md) for more details.

**Composability**
As Perfetto is designed both for OS-level tracing and app-level tracing, its
design allows to compose several instances of the Perfetto tracing library,
allowing to nest multiple layers of tracing and drive then with the same
frontend. This allows powerful blending of app-specific and OS-wide trace
events.
See [docs/multi-layer-tracing.md](docs/multi-layer-tracing.md) for more details.

**Portability**
The only dependencies of Perfetto's tracing libraries are C++11 and [Protobuf lite][protobuf] (plus google-test, google-benchmark, libprotobuf-full for testing).

**Extensibility**
Perfetto allows third parties to defined their own protobufs for:
* [(input) Configuration](https://android.googlesource.com/platform/external/perfetto/+/master/protos/perfetto/config/data_source_config.proto#52)
* [(output) Trace packets](https://android.googlesource.com/platform/external/perfetto/+/master/protos/perfetto/trace/trace_packet.proto#36)

Allowing apps to define their own strongly-typed input and output schema.
See [docs/trace-format.md](docs/trace-format.md) for more details.


Docs
----
#### [Contributing](docs/contributing.md)
#### [Build instructions](docs/build-instructions.md)
#### [Running tests](docs/testing.md)
#### [Running on Android](docs/running-perfetto.md)
#### [Key concepts and architecture](docs/architecture.md)
#### [Life of a trace packet](docs/life-of-a-trace-packet.md)
#### [Performance benchmarks](docs/benchmarks.md)
#### [Trace format](docs/trace-format.md)
#### [Multi-layer tracing](docs/multi-layer-tracing.md)
#### [Security-model](docs/security-model.md)
#### [API surface](docs/api.md)

[ftrace]: https://www.kernel.org/doc/Documentation/trace/ftrace.txt
[systrace]: https://developer.android.com/studio/command-line/systrace.html
[chrome-tracing]: https://www.chromium.org/developers/how-tos/trace-event-profiling-tool
[protobuf]: https://developers.google.com/protocol-buffers/
