# heapprofd - Android Heap Profiler

_These are temporary instructions while heapprofd is under development. They are
subject to frequent change and will be obsoleted once heapprofd is integrated
into Perfetto._

Currently heapprofd only works with SELinux disabled. The `tools/heap_profile`
script takes care of disabling and re-enabling it.

Use the `tools/heap_profile` script to heap profile a process. See all the
arguments using `tools/heap_profile -h`, or use the defaults and just profile a
process (e.g. `system_server`):

```
tools/heap_profile --name system_server
```

This will create a heap dump every second for a default of 1 minute.
