---
role: product
untested: a two-line wrapper; docs_test checks the text it prints through default_settings_json
stamp: source 54c99628, stand-in f5de3d9a
---
# module: gen_default_settings

A build-time tool: prints [default_settings_json](../src/app/settings.hpp.skel.md#function-default_settings_json) to standard output, and exits 0, or 1 when the write fails. CMake runs it to produce the installed reference `settings.json` ([config/settings.json](../config/settings.json.skel.md)). It links `mod_core`, so it always prints the schema the binary was built with.

- **Owns:** nothing.
- **Access:** run by the build.
- **Required:** conditional — when installing.
- **Failure modes:** a failed write exits 1, which fails the build step.
- **Depends on:** [default_settings_json](../src/app/settings.hpp.skel.md#function-default_settings_json)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../CMakeLists.txt.skel.md)

## function: main

- **Inputs:** none.
- **Returns:** the exit status.
- **State changes:** writes the JSON to standard output.
- **Access:** the build.
