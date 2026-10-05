---
role: test
stamp: source 08bd78a9, stand-in f317e894
---
# module: startup_test

The saved settings, keys, darkness and colors apply, and the command line's setting and color over them for the session (the session color never among the saved overrides); bad color entries give the first in file order and a count, and the colors' warning wins over the keys'; the keys' warning stands when the colors are fine.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [load_configuration](../src/app/startup.hpp.skel.md#function-load_configuration)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
