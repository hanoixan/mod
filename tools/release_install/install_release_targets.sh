#!/usr/bin/env bash
# Installs an already-built mod release from GitHub on several machines, one after another:
#   tools/release_install/install_release_targets.sh [--system] VERSION TARGET...
# VERSION: the release's version as tagged, without the v (1.2.0, or a candidate, 1.2.0-rc.1).
# TARGET:
#   wsl             this WSL instance (install_release.sh, here)
#   windows         the Windows host this WSL runs on (install_release.ps1, through powershell.exe)
#   ssh:<dest>      a Linux or macOS machine over ssh (install_release.sh, copied there and run);
#                   <dest> is anything ssh takes, such as a Host from ~/.ssh/config or user@host
# Each target installs for its user by default: ~/.local, or %LOCALAPPDATA%\Programs\mod.
# --system installs the release's package with sudo instead, asking for each machine's password,
# so it runs only from a terminal; it is for Linux targets (wsl, ssh to Linux), and any macOS or
# Windows target stops the run before anything is installed. Every target is tried; the run
# fails if any did.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
say() { printf 'install_release_targets: %s\n' "$*"; }
die() { say "$*" >&2; exit 1; }
usage() {
    sed -n '2,/^[^#]/p' "$0" | sed -n 's/^# \{0,1\}//p' >&2
    exit "${1:-2}"
}

system=0
version=
targets=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --system) system=1 ;;
        -h | --help) usage 0 ;;
        -*) usage ;;
        *) if [[ -z $version ]]; then version=$1; else targets+=("$1"); fi ;;
    esac
    shift
done
[[ -n $version && ${#targets[@]} -gt 0 ]] || usage
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$ ]] || die "not a release version: $version (want one like 1.2.0 or 1.2.0-rc.1)"
for t in "${targets[@]}"; do
    case $t in
        wsl | windows) ;;
        ssh:?*) ;;
        *) die "unknown target: $t (want wsl, windows or ssh:<dest>)" ;;
    esac
done
if [[ $system == 1 && ! (-t 0 && -t 1) ]]; then
    die "--system asks for passwords: run it from your terminal"
fi
args=()
[[ $system == 1 ]] && args+=(--system)
args+=("$version")

is_wsl() { grep -qi microsoft /proc/sys/kernel/osrelease 2>/dev/null; }

# powershell.exe, run through WSL's interop. Where the binfmt entry that runs Windows programs is
# missing (as in some sandboxes), WSL's /init still runs them when given the program.
powershell() {
    local exe
    exe=$(command -v powershell.exe 2>/dev/null || echo /mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe)
    if "$exe" -NoProfile -Command exit >/dev/null 2>&1; then
        "$exe" "$@"
    else
        /init "$exe" "$@"
    fi
}

# Each target's OS, asked before anything is installed, so --system can refuse up front.
os_of() {
    case $1 in
        wsl) is_wsl || die "wsl: this is not a WSL instance"; uname -s ;;
        windows)
            is_wsl || die "windows: the Windows host is reached from WSL only"
            powershell -NoProfile -Command 'exit 0' >/dev/null 2>&1 || die "windows: cannot run powershell.exe"
            echo Windows ;;
        ssh:*) ssh "${1#ssh:}" uname -s </dev/null | tr -d '\r' || die "$1: cannot reach it over ssh" ;;
    esac
}
declare -A os
for t in "${targets[@]}"; do
    os[$t]=$(os_of "$t")
    case ${os[$t]} in
        Linux | Darwin | Windows) ;;
        *) die "$t: unsupported OS ${os[$t]}" ;;
    esac
    if [[ $system == 1 && ${os[$t]} != Linux ]]; then
        die "$t is ${os[$t]}: --system is for Linux targets only; run those apart from it"
    fi
done

install_wsl() { "$here/install_release.sh" "${args[@]}"; }

install_windows() {
    local tmp win
    tmp=$(wslpath -u "$(powershell -NoProfile -Command '[IO.Path]::GetTempPath()' | tr -d '\r')")
    tmp=$tmp/mod-install-release-$$
    mkdir -p "$tmp"
    cp "$here/install_release.ps1" "$tmp/"
    win=$(wslpath -w "$tmp/install_release.ps1")
    local rc=0
    powershell -NoProfile -ExecutionPolicy Bypass -File "$win" -Version "$version" || rc=$?
    rm -rf "$tmp"
    return $rc
}

install_ssh() {
    local dest=$1 tmp rc=0 tty=()
    [[ $system == 1 ]] && tty=(-t)  # sudo asks for the password on a terminal
    tmp=$(ssh "$dest" 'mktemp -d' </dev/null | tr -d '\r')
    [[ -n $tmp ]] || return 1
    scp -q "$here/install_release.sh" "$dest:$tmp/install_release.sh" || rc=$?
    if [[ $rc == 0 ]]; then
        # The arguments were checked above: a version and maybe --system, nothing to quote.
        ssh "${tty[@]}" "$dest" "sh $tmp/install_release.sh ${args[*]}" || rc=$?
    fi
    ssh "$dest" "rm -rf $tmp" </dev/null || true
    return $rc
}

failed=()
for t in "${targets[@]}"; do
    say "== $t (${os[$t]}): mod $version"
    case $t in
        wsl) install_wsl ;;
        windows) install_windows ;;
        ssh:*) install_ssh "${t#ssh:}" ;;
    esac && say "$t: done" || { say "$t: FAILED"; failed+=("$t"); }
done
if [[ ${#failed[@]} -gt 0 ]]; then
    die "failed on: ${failed[*]}"
fi
say "installed mod $version on: ${targets[*]}"
