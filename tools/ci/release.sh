#!/usr/bin/env bash
# The checks and names the release workflows share; run from the repository root, with the
# tags fetched. A candidate is tagged v<version>-rc.<n>, the release v<version>.
#   check-version <version>  MAJOR.MINOR.PATCH, the one in CMakeLists.txt, and not released yet
#   next-rc <version>        the next candidate's number for <version>, 1 for the first
#   rc-of <tag>              the version of candidate <tag> (v<version>-rc.<n>)
#   previous-release         the newest release tag, or nothing before the first release
set -euo pipefail

die() { echo "release.sh: $*" >&2; exit 1; }

released() { git rev-parse -q --verify "refs/tags/v$1" >/dev/null; }

case "${1:-}" in
    check-version)
        version=${2:?usage: release.sh check-version VERSION}
        [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "the version must be MAJOR.MINOR.PATCH, such as 1.2.0 (got '$version')"
        cmake_version=$(sed -n 's/^project(mod VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
        [[ "$cmake_version" == "$version" ]] || die "CMakeLists.txt says $cmake_version; bump it on main first, or use $cmake_version"
        ! released "$version" || die "v$version is already released"
        ;;
    next-rc)
        version=${2:?usage: release.sh next-rc VERSION}
        ! released "$version" || die "v$version is already released"
        last=0
        while read -r tag; do
            n=${tag##*-rc.}
            [[ "$n" =~ ^[0-9]+$ ]] && (( n > last )) && last=$n
        done < <(git tag -l "v$version-rc.*")
        echo $((last + 1))
        ;;
    rc-of)
        tag=${2:?usage: release.sh rc-of TAG}
        [[ "$tag" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)-rc\.[0-9]+$ ]] || die "'$tag' is not a release candidate's tag (v1.2.0-rc.1)"
        echo "${BASH_REMATCH[1]}"
        ;;
    previous-release)
        git tag -l 'v*' | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -n 1 || true
        ;;
    *)
        die "usage: release.sh check-version VERSION | next-rc VERSION | rc-of TAG | previous-release"
        ;;
esac
