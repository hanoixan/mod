#!/usr/bin/env bash
# Joins an arm64 and an x86_64 macOS build into one universal binary and packages it as
# dist/mod-<version>-macos-universal.tar.gz, the same tree as the Linux tarball.
#
#     tools/ci/package_macos.sh <arm64 install prefix> <x86_64 install prefix>
#
# Each prefix is a `cmake --install` of its build. Run from the repository root.
set -euo pipefail

[[ $# -eq 2 ]] || { echo "usage: package_macos.sh <arm64 prefix> <x86_64 prefix>" >&2; exit 2; }
arm=$1
intel=$2
root=$(pwd)
dist=$root/dist
version=$(sed -n 's/^project(mod VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
[[ -n "$version" ]] || { echo "package_macos.sh: no version in CMakeLists.txt" >&2; exit 1; }
name=mod-$version-macos-universal
stage=$(mktemp -d)/$name

mkdir -p "$stage"
cp -R "$arm/." "$stage/"
lipo -create "$arm/bin/mod" "$intel/bin/mod" -output "$stage/bin/mod"
chmod 755 "$stage/bin/mod"  # build artifacts lose the executable bit
lipo -verify_arch "$stage/bin/mod" arm64 x86_64
# Nothing but the system's libraries (libc++ included): nothing from Homebrew.
if otool -L "$stage/bin/mod" | tail -n +2 | grep -v -E '^\s*/usr/lib/|^\s*/System/'; then
  echo "package_macos.sh: mod links a library outside the system" >&2
  exit 1
fi
mkdir -p "$dist"
tar -C "$(dirname "$stage")" -czf "$dist/$name.tar.gz" "$name"
echo "wrote dist/$name.tar.gz"
