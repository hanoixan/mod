#!/bin/sh
# Installs an already-built mod release from GitHub on this Linux (x86_64) or macOS machine:
#   tools/release_install/install_release.sh [--system] [--prefix DIR] VERSION
# VERSION is the release's version as tagged, without the v: 1.2.0, or a candidate, 1.2.0-rc.1.
# By default it unpacks the release's tarball into DIR (~/.local), for this user, with no sudo.
# --system (Linux only) installs the release's deb, rpm or pacman package with sudo instead.
# install.sh at the repository's root, the README's one-liner, is the installer for users; this
# one is for developers, and never picks the version or the kind of install by itself.
set -eu

releases=${MOD_RELEASES:-https://github.com/hanoixan/mod/releases}

say() { printf 'install_release: %s\n' "$*"; }
die() { say "$*" >&2; exit 1; }
usage() {
    echo "usage: $0 [--system] [--prefix DIR] VERSION   (VERSION: 1.2.0 or 1.2.0-rc.1)" >&2
    exit "${1:-2}"
}

system=0
prefix=
version=
while [ $# -gt 0 ]; do
    case $1 in
        --system) system=1 ;;
        --prefix) [ $# -ge 2 ] || usage; prefix=$2; shift ;;
        --prefix=*) prefix=${1#--prefix=} ;;
        -h | --help) usage 0 ;;
        -*) usage ;;
        *) [ -z "$version" ] || usage; version=$1 ;;
    esac
    shift
done
[ -n "$version" ] || usage
printf '%s\n' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$' ||
    die "not a release version: $version (want one like 1.2.0 or 1.2.0-rc.1)"
[ "$system" = 0 ] || [ -z "$prefix" ] || die "--system installs a package, which has no prefix: give one or the other"
prefix=${prefix:-$HOME/.local}
plain=${version%-rc.*}  # the files in a release carry the plain version, a candidate's too

os=$(uname -s)
case $os in
    Linux) [ "$(uname -m)" = x86_64 ] || die "only x86_64 builds are published for Linux (this is $(uname -m))" ;;
    Darwin) [ "$system" = 0 ] || die "--system is for Linux only: on macOS mod installs for this user" ;;
    *) die "this script is for Linux and macOS (on Windows, use install_release.ps1)" ;;
esac
command -v curl >/dev/null 2>&1 || die "curl is needed"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
fetch() {
    url=$releases/download/v$version/$1
    say "downloading $url"
    curl -fsSL -o "$tmp/$1" "$url" || die "cannot download $url (is $version released, with that file?)"
}

# The installed mod must say the version asked for.
check() {
    got=$("$1" --version 2>&1) || die "$1 --version failed: $got"
    case $got in
        *" $plain"*) say "installed $1: $got" ;;
        *) die "$1 says \"$got\", not $plain" ;;
    esac
}

if [ "$system" = 1 ]; then
    if [ "$(id -u)" = 0 ]; then
        sudo=
    else
        command -v sudo >/dev/null 2>&1 || die "--system needs root or sudo"
        sudo=sudo
    fi
    if command -v apt-get >/dev/null 2>&1; then
        fetch "mod_${plain}_amd64.deb"
        chmod 644 "$tmp/mod_${plain}_amd64.deb"  # apt's sandbox user reads it
        chmod 755 "$tmp"
        $sudo apt-get install -y --allow-downgrades "$tmp/mod_${plain}_amd64.deb"
    elif command -v dnf >/dev/null 2>&1; then
        fetch "mod-${plain}-1.x86_64.rpm"
        $sudo dnf install -y "$tmp/mod-${plain}-1.x86_64.rpm"
    elif command -v pacman >/dev/null 2>&1; then
        fetch "mod-${plain}-1-x86_64.pkg.tar.zst"
        $sudo pacman -U --noconfirm "$tmp/mod-${plain}-1-x86_64.pkg.tar.zst"
    else
        die "--system needs apt-get, dnf or pacman; without --system mod installs into ~/.local"
    fi
    check /usr/bin/mod
    exit 0
fi

if [ "$os" = Darwin ]; then
    name=mod-$plain-macos-universal
else
    name=mod-$plain-linux-x86_64
fi
fetch "$name.tar.gz"
tar -xzf "$tmp/$name.tar.gz" -C "$tmp"
mkdir -p "$prefix"
cp -R "$tmp/$name/." "$prefix/"
check "$prefix/bin/mod"
case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *) say "$prefix/bin is not on your PATH; add it, for example: export PATH=\"$prefix/bin:\$PATH\"" ;;
esac
