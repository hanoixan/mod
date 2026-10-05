---
role: product
unit: ./line_scanner.hpp.skel.md
stamp: source f85d5b7c, stand-in 2ea30db6
---
# module: line_scanner (implementation)

Implements [LineScanner](./line_scanner.hpp.skel.md#class-linescanner) as a `std::jthread` loop over the chunks: `count('\n')`, then `hasher.update(chunk)`, then batch and post. The hash covers the whole file, so it is only valid if the scan completed without cancellation.

- **Owns:** the thread body.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** an exception inside the thread, such as `bad_alloc` while batching, must be caught. Log it, post a "scan failed" result (`on_done` with an `internal` error; posting it must not throw), and leave line numbers unknown and history unverified.
- **Depends on:** [LineScanner](./line_scanner.hpp.skel.md#class-linescanner)
- **Depends on:** [log](../util/log.hpp.skel.md#function-log)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
