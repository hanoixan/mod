---
role: test
stamp: source c189e380, stand-in 4a5c5b57
---
# module: fuzz_color_spec

A libFuzzer target (`LLVMFuzzerTestOneInput`) for color specs from settings files and the command line: never crash. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_color_spec`, which ctest runs over `fuzz/corpus/color_spec/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [parse_color_spec](../src/ui/theme.hpp.skel.md#function-parse_color_spec)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
