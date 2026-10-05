---
role: test
stamp: source 36f752bf, stand-in 1bc63ab9
---
# module: fuzz_lsp_frames

A libFuzzer target (`LLVMFuzzerTestOneInput`) for what a language server writes, in reads of any sizes: framing never crashes or buffers past its limit. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_lsp_frames`, which ctest runs over `fuzz/corpus/lsp_frames/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [FrameParser](../src/syntax/lsp_client.hpp.skel.md#class-frameparser)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
