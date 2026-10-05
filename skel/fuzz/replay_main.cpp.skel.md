---
role: test
stamp: source 1fc7c121, stand-in 7f092d21
---
# module: replay_main

A `main` for the replay tests: runs every file of the corpus folders (or files) given as arguments, in name order, through the target's `LLVMFuzzerTestOneInput`, so every input a fuzzer ever needed, crashes found and fixed included, stays in the suite of every build (gcc too). Exits 1 when it found no input, so a missing corpus fails.

- **Owns:** nothing.
- **Access:** ctest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** a target's property failing traps, which fails the test.
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
