---
role: test
stamp: source b5b460cb, stand-in 17a9791d
---
# module: fuzz_editor

A libFuzzer target (`LLVMFuzzerTestOneInput`) for random editing sessions: any sequence of the editor's commands with history moves: the cursor and selection stay inside the text and on character boundaries (never inside a valid multi-byte sequence; an invalid byte is a character of its own), and undoing everything gives back the empty start. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_editor`, which ctest runs over `fuzz/corpus/editor/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [Editor](../src/edit/editor.hpp.skel.md#class-editor)
- **Depends on:** [Document](../src/edit/document.hpp.skel.md#class-document)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
