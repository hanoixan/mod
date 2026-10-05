---
role: test
stamp: source 99c00a8a, stand-in db5e22da
---
# module: fuzz_regex

A libFuzzer target (`LLVMFuzzerTestOneInput`) for a search pattern, options and replacement from the user, matched over any text: compiling, finding every match of every line with a LineCursor, and expanding the replacement never crash, matches lie inside the text and move forward, and PCRE2's limits keep each search bounded. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_regex`, which ctest runs over `fuzz/corpus/regex/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [Regex](../src/search/regex.hpp.skel.md#class-regex)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
