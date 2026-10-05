---
role: manifest
stamp: source 4d8514ae, stand-in 48d709c9
---
# data: CMakePresets

CMake presets for repeatable configure, build and test on every platform. The Linux presets pin `g++-13`, because the default `g++` on the build machine (Ubuntu 22.04) is 11.4.

- **Source:** hand-authored.
- **Required:** optional — without it developers must pass the generator and compiler by hand.
- **Failure modes:** `g++-13` is not on `PATH` on another Linux machine, so configure fails with a clear message. Developers can override with a `CMakeUserPresets.json`, which is git-ignored. There is no Windows preset in this phase. Windows builds will use the MSYS2 MSYS environment; its preset is added when a Windows build is made.
- **Depends on:** [CMakeLists.txt](./CMakeLists.txt.skel.md)
- **Referred by:** none known (consumed by developers and CI via `cmake --preset`)
- **Unknowns:** none

## Schema

```jsonc
{
  "version": 6,                       // CMake >= 3.25
  "configurePresets": [
    { "name": "base", "hidden": true, "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_EXPORT_COMPILE_COMMANDS": "ON" } },
    { "name": "linux-debug",   "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "CMAKE_CXX_COMPILER": "g++-13", "MOD_SANITIZE": "ON" } },
    { "name": "linux-release", "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release", "CMAKE_CXX_COMPILER": "g++-13" } },
    { "name": "macos-release", "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Darwin" } }
    // No Windows preset in this phase (MSYS2 MSYS environment, later).
  ],
  "buildPresets": [ { "name": "linux-debug", "configurePreset": "linux-debug" } /* ...one per configure preset */ ],
  "testPresets":  [ { "name": "linux-debug", "configurePreset": "linux-debug",
                      "output": { "outputOnFailure": true },
                      "filter": { "exclude": { "label": "stress" } } } /* ...one per configure preset */,
                    // The gigabyte files through the editor, and only those (tests labelled stress).
                    { "name": "linux-release-stress", "configurePreset": "linux-release",
                      "output": { "outputOnFailure": true, "verbosity": "verbose" },
                      "filter": { "include": { "label": "stress" } } } ]
}
```
