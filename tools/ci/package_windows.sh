#!/usr/bin/env bash
# Packages a Windows build (the MSYS2 runtime) into dist/mod-<version>-windows-x86_64.zip:
# bin/mod.exe with msys-2.0.dll beside it, and share/mod (the manual, the reference
# settings), the same tree as the Linux tarball. Run in an MSYS2 shell from the repository
# root, after `cmake --build --preset windows-release`.
set -euo pipefail

root=$(pwd)
build=$root/build/windows-release
dist=$root/dist
version=$(sed -n 's/^project(mod VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
[[ -n "$version" ]] || { echo "package_windows.sh: no version in CMakeLists.txt" >&2; exit 1; }
name=mod-$version-windows-x86_64
stage=$build/stage/$name

rm -rf "$build/stage"
cmake --install "$build" --prefix "$stage"
cp /usr/bin/msys-2.0.dll "$stage/bin/"
mkdir -p "$dist"
rm -f "$dist/$name.zip"
(cd "$build/stage" && zip -qr "$dist/$name.zip" "$name")
echo "wrote dist/$name.zip"
