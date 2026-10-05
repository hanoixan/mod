---
role: test
stamp: source c28743a3, stand-in 32e1675f
---
# module: fuzz_cli

A libFuzzer target (`LLVMFuzzerTestOneInput`) for command lines, the input split at NUL bytes into arguments: never crash. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_cli`, which ctest runs over `fuzz/corpus/cli/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [parse_cli](../src/app/cli_options.hpp.skel.md#function-parse_cli)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
