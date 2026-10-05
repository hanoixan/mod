---
role: product
untested: a CI script; release_test.sh exercises it, and CI runs that
stamp: source 13ec8fe7, stand-in cce1b3d8
---
# module: release.sh

The checks and names the release workflows share, kept out of the YAML so they can be tested ([release_test.sh](./release_test.sh.skel.md)). Run from the repository root with the tags fetched. A release candidate is tagged `v<version>-rc.<n>`, a release `v<version>`.

- `check-version <version>`: fails, saying why, unless `version` is `MAJOR.MINOR.PATCH`, equals `project(mod VERSION …)` in CMakeLists.txt, and `v<version>` does not exist (not released yet).
- `next-rc <version>`: prints the next candidate's number for `version`: one more than the highest `n` of its `v<version>-rc.<n>` tags, compared as numbers, 1 for the first; fails once `v<version>` exists.
- `rc-of <tag>`: prints the version of a candidate's tag (`v1.2.0-rc.2` gives `1.2.0`); fails for anything else.
- `previous-release`: prints the newest release tag by version (`sort -V`) among `vX.Y.Z` tags, candidates left out; nothing before the first release.

- **Owns:** nothing.
- **Access:** the release candidate and promote workflows, release_test.sh.
- **Required:** yes, for releases.
- **Failure modes:** a check that fails prints `release.sh: <reason>` and exits 1, which stops the workflow before anything is tagged.
- **Depends on:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Unknowns:** none
- **Referred by:** [package.sh](./package.sh.skel.md)
- **Referred by:** [release_test.sh](./release_test.sh.skel.md)
