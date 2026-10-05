---
role: test
stamp: source c222407a, stand-in 4400f232
---
# module: fuzz_sidecar

A libFuzzer target (`LLVMFuzzerTestOneInput`) for a .history history file of any content beside a document (one could come with a cloned repository): opening it never crashes or changes the document, whatever history loads can be walked, and editing goes on. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_sidecar`, which ctest runs over `fuzz/corpus/sidecar/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [Document](../src/edit/document.hpp.skel.md#class-document)
- **Depends on:** [Sidecar](../src/edit/sidecar.hpp.skel.md#class-sidecar)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
