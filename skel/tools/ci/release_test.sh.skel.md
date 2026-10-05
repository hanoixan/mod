---
role: test
stamp: source d74ad5ea, stand-in 25ce29d6
---
# module: release_test.sh

Tests [release.sh](./release.sh.skel.md) against a scratch git repository (removed on exit): `check-version` accepts the version in CMakeLists.txt and refuses another, a malformed one and one already released; `next-rc` counts from 1, takes the highest candidate of that version by number (rc.10 after rc.2), ignores other versions' candidates, and refuses a released version; `rc-of` parses a candidate's tag and refuses a release's or a malformed one; `previous-release` is empty before the first release, ignores candidates and orders by version. Prints each failure and exits non-zero if there was any. CI's linux job runs it.

- **Owns:** its scratch repository.
- **Access:** CI, developers.
- **Required:** optional.
- **Failure modes:** none.
- **Depends on:** [release.sh](./release.sh.skel.md)
- **Unknowns:** none
- **Referred by:** none (CI's linux job runs it; the checker does not read dot-directories)
