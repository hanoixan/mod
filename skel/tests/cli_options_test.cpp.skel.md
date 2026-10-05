---
role: test
stamp: source d7099007, stand-in 5668f8f8
---
# module: cli_options_test

Paths and `--`; help and version; `-ro` and `--read-only`; `--persist-history`; every scalar setting by its flag; the boolean forms (`--x`, `--no-x`, `=on`/`off`); a choice with dashes for spaces; out-of-range, missing and bad values; unknown flags; repeated `--color` in both forms and a bad color name or spec; a later flag winning; the usage naming every scalar setting.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [parse_cli](../src/app/cli_options.hpp.skel.md#function-parse_cli)
- **Depends on:** [cli_usage](../src/app/cli_options.hpp.skel.md#function-cli_usage)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
