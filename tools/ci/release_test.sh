#!/usr/bin/env bash
# Tests release.sh against a scratch git repository: candidate numbering, the version checks
# and finding the previous release. Exits non-zero on the first failure.
set -euo pipefail

script=$(cd "$(dirname "$0")" && pwd)/release.sh
repo=$(mktemp -d)
trap 'rm -rf "$repo"' EXIT
cd "$repo"
git init -q
git config user.name test
git config user.email test@example.com
printf 'project(mod VERSION 1.2.0 LANGUAGES CXX)\n' > CMakeLists.txt
git add CMakeLists.txt
git commit -qm one

failures=0
expect() {  # expect <want> <command...>: the command succeeds and prints <want>
    local want=$1 got
    shift
    if ! got=$("$@" 2>&1); then
        echo "FAIL: $* failed: $got"; failures=$((failures + 1)); return
    fi
    [[ "$got" == "$want" ]] || { echo "FAIL: $*: want '$want', got '$got'"; failures=$((failures + 1)); }
}
refuse() {  # refuse <command...>: the command fails
    if "$@" >/dev/null 2>&1; then echo "FAIL: $* succeeded"; failures=$((failures + 1)); fi
}

expect "" "$script" check-version 1.2.0
refuse "$script" check-version 1.2          # not MAJOR.MINOR.PATCH
refuse "$script" check-version 1.3.0        # not what CMakeLists.txt says
refuse "$script" check-version v1.2.0

expect 1 "$script" next-rc 1.2.0
git tag v1.2.0-rc.1
git tag v1.2.0-rc.2
git tag v1.1.0-rc.7
expect 3 "$script" next-rc 1.2.0
git tag v1.2.0-rc.10
expect 11 "$script" next-rc 1.2.0           # by number, not as text

expect 1.2.0 "$script" rc-of v1.2.0-rc.2
refuse "$script" rc-of v1.2.0
refuse "$script" rc-of v1.2.0-rc.
refuse "$script" rc-of 1.2.0-rc.2

expect "" "$script" previous-release        # no release yet
git tag v1.0.0
git tag v1.1.0
git tag v1.10.0-rc.1                        # a candidate is not a release
expect v1.1.0 "$script" previous-release
git tag v1.2.0
refuse "$script" check-version 1.2.0        # already released
refuse "$script" next-rc 1.2.0              # no candidates after the release
expect v1.2.0 "$script" previous-release

if (( failures > 0 )); then echo "$failures failed"; exit 1; fi
echo "release.sh: all passed"
