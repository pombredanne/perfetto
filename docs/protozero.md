ProtoZero
---------

*** note
**This doc is WIP**, stay tuned.
<!-- TODO(primiano): write protozero doc. -->
***

ProtoZero is an almost* zero-copy zero-malloc append-only protobuf library.
It's designed to be fast and efficient at the cost of a reduced API
surface for generated stubs. The main limitations consist of:
- Append-only interface: no readbacks are possible from the stubs.
- No runtime checks for duplicated or missing mandatory fields.
- Mandatory ordering when writing of nested messages: once a nested message is
  started it must be completed before adding any fields to its parent.

***
* Allocations and library calls will happen only when crossing the boundary of a
contiguous buffer (e.g., to request a new buffer to continue the write).
***

Other resources
---------------
* [Design doc](https://goo.gl/EKvEfa])
