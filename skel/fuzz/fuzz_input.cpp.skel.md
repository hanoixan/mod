---
role: test
stamp: source 06fc92f2, stand-in fea22ecc
---
# module: fuzz_input

A libFuzzer target (`LLVMFuzzerTestOneInput`) for terminal input in reads of any sizes, with timeouts between: the decoder never crashes and never turns bytes into a typed C0 control or DEL (a C1 character sent as UTF-8 is text, kept, and shown escaped). A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_input`, which ctest runs over `fuzz/corpus/input/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [InputDecoder](../src/ui/input.hpp.skel.md#class-inputdecoder)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
