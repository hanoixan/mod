---
role: product
untested: a build script; CI runs it on every push and smoke tests the tarball it makes
---
# module: package_macos.sh

Joins an arm64 and an x86_64 macOS build into one universal binary: `tools/ci/package_macos.sh <arm64 prefix> <x86_64 prefix>`, each a `cmake --install` of a native build (Homebrew LLVM with its own libc++, linked statically, macOS 13 deployment target). It copies the arm64 tree, replaces `bin/mod` with `lipo -create` of both (made executable again, as build artifacts lose the bit), checks both architectures are in it (`lipo -verify_arch`) and that it links nothing outside `/usr/lib` and `/System` (`otool -L`), and writes `dist/mod-<version>-macos-universal.tar.gz` with one top folder of that name, the Linux tarball's tree.

- **Owns:** a temporary staging folder, the tarball in `dist/`.
- **Access:** CI and the release candidate workflow, on macOS.
- **Required:** for macOS releases.
- **Failure modes:** wrong arguments; a missing architecture; a library from outside the system (Homebrew's libc++ not linked in): each stops the script.
- **Referred by:** [install.sh](../../install.sh.skel.md)
- **Depends on:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Unknowns:** none
