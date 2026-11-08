---
role: test
stamp: source ad08567e, stand-in 75252769
---
# module: notices_test

A notice without a lifetime stays; a timed one waits to be shown, then ends that long after, giving its deadline meanwhile; posting replaces, `clear` and `retain` drop, `posted` reports the posting.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none; the times are passed in, never waited for.
- **Depends on:** [Notices](../src/app/notices.hpp.skel.md#class-notices)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
