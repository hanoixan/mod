---
role: manifest
kind: resource
stamp: source 7d59519e, stand-in d3325455
---
# resource: CONTRIBUTING.md

How work reaches main and how a release is made, for people working on mod.

Contents: **GitHub Flow**: branch off `main`, push, open a pull request, wait for `.github/workflows/ci.yml` to pass, merge, delete the branch; `main` is always releasable. Branch protection is not enforced while the repository is private on a free plan (GitHub refuses it), so the rule is by agreement, and it can be enforced later under Settings > Branches. **Releasing**: bump `project(mod VERSION …)` in CMakeLists.txt in a pull request, merge it, then run Actions > Release candidate on `main` with that version, try the prerelease (`MOD_VERSION=<version>-rc.<n>` for the one-liner), and run Actions > Promote release with its tag and the version typed to confirm, which publishes the candidate's files unchanged; why the files carry the plain version (and so the candidate's `--version` and `PKGBUILD`); deleting a tag after a failed run; the approval GitHub offers only on public or Enterprise repositories. The release also carries `mod-<version>-linux-x86_64.tar.gz`, which [install.sh](./install.sh.skel.md) unpacks when it cannot use a package manager. **Pictures**: regenerate the README's with [tools/screenshot.py](./tools/screenshot.py.skel.md). **Building packages locally**: [tools/ci/package.sh](./tools/ci/package.sh.skel.md). **The other CI checks locally**: the commands for the fuzz build and a fuzzer run (copying the corpus first unless its finds are meant to be kept; a fixed crash goes into `fuzz/corpus/<target>/` as `regression-<name>`), clang-tidy, [tools/ci/valgrind.sh](./tools/ci/valgrind.sh.skel.md) over a build configured with `-DPCRE2_SUPPORT_VALGRIND=ON`, and the [stress tests](./tests/stress_test.cpp.skel.md) (`ctest --preset linux-release-stress`; about 10 GB of disk; `MOD_STRESS_DIR` and `MOD_STRESS_BYTES`).

- **Required:** optional.
- **Failure modes:** none.
- **Depends on:** none
- **Referred by:** none known (read by people)
- **Unknowns:** none
