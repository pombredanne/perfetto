This directory contains Android code that accesses (hw)binder interfaces
and is dynamically loaded and used by traced_probes.

Code in this directory:
- Can depend on Android internals, as it is built by in-tree builds
  (via Android.bp).
- Has no-op implementations for out-of-tree (standalone) builds, which 
  allow dependencies to build cleanly without bubbling up #ifdefs.
- Is dynamically loaded via dlopen()/dlsym() by the rest of perfetto.

The latter is to avoid paying the cost of linker relocations
(~150 MB private dirty) requires by loading libhidlbase.so,
libhidltransport.so, libhwbinder.so and their recursive dependencies,
until the first time a data source that requires binder is used.

The general structure and rules for code in this directory is as-follows:
- Targets herein defined must be leaf targets. Dependencies to perfetto targets
  (e.g. base) are not allowed, as doing that would create ODR violations.
- Headers (e.g. health_hal.h) must have a plain old C interface (to avoid
  dealing with name mangling) and should not expose neither android internal
  structure/types nor struct/types defined in perfetto headers outside of this
  directory.
- Dependencies to Android internal headers are allowed only in .cc files, not
  in headers.
- Each API should have a default _noop.cc implementation for standalone builds.
