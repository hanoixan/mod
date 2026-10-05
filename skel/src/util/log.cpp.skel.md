---
role: product
unit: ./log.hpp.skel.md
stamp: source b0e5a075, stand-in 6cbbea3f
---
# module: log (implementation)

Implements [log.hpp](./log.hpp.skel.md) with a function-local static sink: a `FILE*` plus a mutex. Flush after each line so that the log survives crashes.

- **Owns:** the log file handle and its mutex.
- **Access:** internal. Reached only through `log.hpp`.
- **Required:** optional — as for [log.hpp](./log.hpp.skel.md).
- **Failure modes:** a write error disables logging for the rest of the process. Never throw from it.
- **Depends on:** [log declarations](./log.hpp.skel.md#function-log)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
