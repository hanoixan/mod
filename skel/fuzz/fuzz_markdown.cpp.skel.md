---
role: test
stamp: source f0289631, stand-in eeda70b8
---
# module: fuzz_markdown

A libFuzzer target (`LLVMFuzzerTestOneInput`) for any file opened as Markdown: the outline scan, the read-only layout and its motions never crash, and every source offset and position lies inside the document. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_markdown`, which ctest runs over `fuzz/corpus/markdown/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [scan_markdown](../src/syntax/markdown.hpp.skel.md#function-scan_markdown)
- **Depends on:** [ReadingLayout](../src/ui/reading_layout.hpp.skel.md#class-readinglayout)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
