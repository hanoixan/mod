---
role: product
untested: a build script; CI runs it on every push and smoke tests the zip it makes
---
# module: package_windows.sh

Packages a Windows build into `dist/mod-<version>-windows-x86_64.zip`, run in an MSYS2 MSYS shell from the repository root after `cmake --build --preset windows-release`. It installs the build (`cmake --install … --prefix`) into a staging folder `mod-<version>-windows-x86_64/`, copies MSYS2's `msys-2.0.dll` (the runtime) into its `bin/` beside `mod.exe`, and zips the folder. The tree is the Linux tarball's: `bin/` and `share/mod` (the manual, found beside the binary). The version comes from `project(mod VERSION …)`.

- **Owns:** `build/windows-release/stage`, the zip in `dist/`.
- **Access:** CI and the release candidate workflow.
- **Required:** for Windows releases.
- **Failure modes:** no version in CMakeLists.txt; no build; no `zip`: each stops the script.
- **Referred by:** [install.ps1](../../install.ps1.skel.md)
- **Depends on:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Unknowns:** none
- **Referred by:** [package.sh](./package.sh.skel.md)
