---
role: test
stamp: source e97e6483, stand-in 7323bf07
---
# module: fuzz_json

A libFuzzer target (`LLVMFuzzerTestOneInput`) for JSON from settings files, language configs and language servers: never crashes; what it writes is valid UTF-8 and reads back; writing is stable from the first write on (only from there: invalid UTF-8 is written as U+FFFD, so names differing only in invalid bytes merge, and a number too large is infinity, written as null). A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_json`, which ctest runs over `fuzz/corpus/json/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [Json](../src/syntax/json.hpp.skel.md#class-json)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
