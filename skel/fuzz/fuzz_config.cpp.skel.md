---
role: test
stamp: source cd052139, stand-in 18f82dd7
---
# module: fuzz_config

A libFuzzer target (`LLVMFuzzerTestOneInput`) for a user's settings.json and languages.json as the editor applies them: colors, key bindings and language entries from any bytes never crash. A property that does not hold stops the fuzzer through [fail](./fuzz_reader.hpp.skel.md#function-fail). Built as a fuzzer with `MOD_BUILD_FUZZERS` (clang), and with `MOD_BUILD_TESTS` linked with [replay_main](./replay_main.cpp.skel.md) into `fuzz_replay_config`, which ctest runs over `fuzz/corpus/config/`.

- **Owns:** nothing beyond one input's run.
- **Access:** libFuzzer, and the replay test.
- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [ColorTheme](../src/ui/theme.hpp.skel.md#class-colortheme)
- **Depends on:** [Keymap](../src/app/keymap.hpp.skel.md#class-keymap)
- **Depends on:** [LanguageConfig](../src/syntax/language_config.hpp.skel.md#class-languageconfig)
- **Depends on:** [Reader](./fuzz_reader.hpp.skel.md#class-reader)
- **Unknowns:** none
- **Referred by:** [fuzz/CMakeLists.txt](./CMakeLists.txt.skel.md)
