---
role: test
stamp: source f9a3d03c, stand-in 8006b6dd
---
# module: fuzz_utf8

A libFuzzer target (`LLVMFuzzerTestOneInput`) for decoding, widths, character classes and grapheme clusters over any bytes, forwards and backwards: every step makes progress and stays inside the input. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_utf8`, which ctest runs over `fuzz/corpus/utf8/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [decode](../src/text/utf8.hpp.skel.md#function-decode)
- **Depends on:** [next_grapheme_boundary](../src/text/utf8.hpp.skel.md#function-next_grapheme_boundary)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
