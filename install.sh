#!/bin/sh
# Installs mod on Linux x86_64:
#   curl -fsSL https://raw.githubusercontent.com/hanoixan/mod/main/install.sh | sh
# With apt-get, dnf or pacman and root or sudo, installs the release's package; otherwise
# (or with MOD_INSTALL_LOCAL=1) unpacks it into MOD_PREFIX, ~/.local by default.
# MOD_VERSION picks a release (default: the latest), or a release candidate such as 1.2.0-rc.1.
set -eu

releases=${MOD_RELEASES:-https://github.com/hanoixan/mod/releases}
api=${MOD_API:-https://api.github.com/repos/hanoixan/mod/releases/latest}
prefix=${MOD_PREFIX:-$HOME/.local}

say() { printf 'mod install: %s\n' "$*"; }
die() { say "$*" >&2; exit 1; }

[ "$(uname -s)" = Linux ] || die "only Linux is supported by this installer"
[ "$(uname -m)" = x86_64 ] || die "only x86_64 builds are published (this is $(uname -m))"
command -v curl >/dev/null 2>&1 || die "curl is needed"

# `release` names the tag (v1.2.0, or v1.2.0-rc.1 for a candidate); `version`, the files in it,
# which carry the plain version either way.
release=${MOD_VERSION:-}
if [ -z "$release" ]; then
    release=$(curl -fsSL "$api" | sed -n 's/.*"tag_name": *"v\([^"]*\)".*/\1/p' | head -n 1) ||
        die "cannot reach $api"
    [ -n "$release" ] || die "no release found at $api"
fi
version=${release%-rc.*}
say "installing mod $release"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
fetch() {
    say "downloading $1"
    curl -fsSL -o "$tmp/$1" "$releases/download/v$release/$1" || die "cannot download $releases/download/v$release/$1"
}

if [ "$(id -u)" = 0 ]; then
    sudo=
elif command -v sudo >/dev/null 2>&1; then
    sudo=sudo
else
    sudo=none
fi

if [ "${MOD_INSTALL_LOCAL:-0}" != 1 ] && [ "$sudo" != none ]; then
    if command -v apt-get >/dev/null 2>&1; then
        fetch "mod_${version}_amd64.deb"
        $sudo apt-get install -y "$tmp/mod_${version}_amd64.deb"
        say "installed with apt; remove it with: ${sudo:+$sudo }apt-get remove mod"
        exit 0
    elif command -v dnf >/dev/null 2>&1; then
        fetch "mod-${version}-1.x86_64.rpm"
        $sudo dnf install -y "$tmp/mod-${version}-1.x86_64.rpm"
        say "installed with dnf; remove it with: ${sudo:+$sudo }dnf remove mod"
        exit 0
    elif command -v pacman >/dev/null 2>&1; then
        fetch "mod-${version}-1-x86_64.pkg.tar.zst"
        $sudo pacman -U --noconfirm "$tmp/mod-${version}-1-x86_64.pkg.tar.zst"
        say "installed with pacman; remove it with: ${sudo:+$sudo }pacman -R mod"
        exit 0
    fi
fi

# No package manager to use: unpack into the prefix.
name=mod-$version-linux-x86_64
fetch "$name.tar.gz"
tar -xzf "$tmp/$name.tar.gz" -C "$tmp"
mkdir -p "$prefix"
cp -R "$tmp/$name/." "$prefix/"
say "installed $prefix/bin/mod"
case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *) say "$prefix/bin is not on your PATH; add it, for example: export PATH=\"$prefix/bin:\$PATH\"" ;;
esac
