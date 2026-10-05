---
role: manifest
stamp: source 7c77162a, stand-in cdb58adf
---
# resource: fuzz/CMakeLists.txt

The fuzz targets' build. For each target name in `MOD_FUZZ_TARGETS`: with `MOD_BUILD_FUZZERS` (clang only; the core is then compiled with `-fsanitize=fuzzer-no-link,address,undefined` for coverage) an executable `fuzz_<name>` linked with `-fsanitize=fuzzer`; with `MOD_BUILD_TESTS` an executable `fuzz_replay_<name>` (the target with [replay_main](./replay_main.cpp.skel.md)) and a ctest `fuzz_replay_<name>` over `corpus/<name>/`. Seeds and every crash input found are kept in `corpus/`. Run a fuzzer, for example: `fuzz_json fuzz/corpus/json -max_total_time=300` (CI runs each for 60 s on every push).

- **Required:** conditional — with `MOD_BUILD_FUZZERS` or `MOD_BUILD_TESTS`.
- **Failure modes:** none.
- **Depends on:** [fuzz_json](./fuzz_json.cpp.skel.md)
- **Depends on:** [fuzz_config](./fuzz_config.cpp.skel.md)
- **Depends on:** [fuzz_color_spec](./fuzz_color_spec.cpp.skel.md)
- **Depends on:** [fuzz_cli](./fuzz_cli.cpp.skel.md)
- **Depends on:** [fuzz_input](./fuzz_input.cpp.skel.md)
- **Depends on:** [fuzz_utf8](./fuzz_utf8.cpp.skel.md)
- **Depends on:** [fuzz_lsp_frames](./fuzz_lsp_frames.cpp.skel.md)
- **Depends on:** [fuzz_regex](./fuzz_regex.cpp.skel.md)
- **Depends on:** [fuzz_markdown](./fuzz_markdown.cpp.skel.md)
- **Depends on:** [fuzz_editor](./fuzz_editor.cpp.skel.md)
- **Depends on:** [fuzz_sidecar](./fuzz_sidecar.cpp.skel.md)
- **Depends on:** [replay_main](./replay_main.cpp.skel.md)
- **Referred by:** [CMakeLists.txt](../CMakeLists.txt.skel.md)
- **Unknowns:** none
