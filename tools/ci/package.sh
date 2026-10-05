#!/usr/bin/env bash
# Builds, tests and packages mod for Linux (.deb, .rpm, Arch .pkg.tar.zst, a tarball) into dist/, then
# installs each package in a fresh container of its own distro and runs it. Run from the
# repository root. MOD_SKIP_PACKAGE_TESTS=1 skips the install checks.
set -euo pipefail

root=$(pwd)
build=$root/build/package
dist=$root/dist
version=$(sed -n 's/^project(mod VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
[[ -n "$version" ]] || { echo "package.sh: no version in CMakeLists.txt" >&2; exit 1; }
echo "== mod $version"

# 1. Build and test. The install prefix is baked into the binary (where F1 finds the manual).
cmake --preset linux-release -B "$build" -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure --label-exclude stress  # the gigabyte files have their own job

# 2. .deb and .rpm.
rm -rf "$dist"
mkdir -p "$dist"
(cd "$build" && cpack -G "DEB;RPM")
cp "$build/mod_${version}_amd64.deb" "$build/mod-${version}-1.x86_64.rpm" "$dist/"

# 3. The Arch package, from the same install tree.
stage=$build/arch
rm -rf "$stage"
mkdir -p "$stage"
DESTDIR="$stage/stage" cmake --install "$build"
sed "s/@VERSION@/$version/" packaging/arch/PKGBUILD-bin.in > "$stage/PKGBUILD"
docker run --rm -v "$stage:/pkg" -v "$dist:/dist" -e HOST_UID="$(id -u)" archlinux:latest bash -euc '
    pacman -Sy --noconfirm --needed base-devel >/dev/null
    useradd -m builder
    cp -r /pkg /home/builder/pkg
    chown -R builder /home/builder/pkg
    su builder -c "cd /home/builder/pkg && makepkg --nodeps --force"
    cp /home/builder/pkg/*.pkg.tar.zst /dist/
    chown "$HOST_UID" /dist/*.pkg.tar.zst'

# 4. A plain tarball of the same tree, for install.sh's ~/.local install.
tarball=mod-$version-linux-x86_64
tar -C "$stage/stage/usr" --owner=0 --group=0 --transform "s,^\.,$tarball," -czf "$dist/$tarball.tar.gz" .

# 5. Each package installs on its own distro and runs.
if [[ "${MOD_SKIP_PACKAGE_TESTS:-0}" != 1 ]]; then
    want="mod $version"
    check() {  # image, install command
        echo "== install check on $1"
        got=$(docker run --rm -v "$dist:/dist:ro" "$1" bash -euc "{ $2; } >/dev/null 2>&1; mod --version")
        [[ "$got" == "$want" ]] || { echo "package.sh: $1 printed '$got', not '$want'" >&2; exit 1; }
    }
    check ubuntu:22.04 "apt-get update && apt-get install -y /dist/mod_${version}_amd64.deb"
    check fedora:latest "dnf install -y /dist/mod-${version}-1.x86_64.rpm"
    check archlinux:latest "pacman -U --noconfirm /dist/mod-${version}-1-x86_64.pkg.tar.zst"
    check debian:12 "tar -xzf /dist/$tarball.tar.gz -C /opt && ln -s /opt/$tarball/bin/mod /usr/local/bin/mod"
fi

echo "== packages in dist/:"
ls -l "$dist"
