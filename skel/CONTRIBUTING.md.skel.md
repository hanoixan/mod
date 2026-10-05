---
role: manifest
kind: resource
stamp: source 5e31c825, stand-in 27936ed3
---
# resource: CONTRIBUTING.md

How work reaches main and how a release is made, for people working on mod.

Contents: **GitHub Flow**: branch off `main`, push, open a pull request, wait for `.github/workflows/ci.yml` to pass, merge, delete the branch; `main` is always releasable. `main` is protected: a pull request whose CI jobs (linux, fuzz, clang-tidy, valgrind, stress) passed, no force-push or deletion, and no approving review required (one cannot approve one's own pull request). **Releasing**: bump `project(mod VERSION …)` in CMakeLists.txt in a pull request, merge it, then run Actions > Release candidate on `main` with that version (it builds, tests and smoke tests Linux, Windows and both macOS architectures, joined into one universal binary, and publishes nothing unless all pass), try the prerelease (`MOD_VERSION=<version>-rc.<n>` for the one-liner), and run Actions > Promote release with its tag and the version typed to confirm, which publishes the candidate's files unchanged; why the files carry the plain version (and so the candidate's `--version` and `PKGBUILD`); deleting a tag after a failed run; the `release` environment's required reviewer approving the promotion. The release also carries `mod-<version>-linux-x86_64.tar.gz`, which [install.sh](./install.sh.skel.md) unpacks when it cannot use a package manager. **Pictures**: regenerate the README's with [tools/screenshot.py](./tools/screenshot.py.skel.md). **Building packages locally**: [tools/ci/package.sh](./tools/ci/package.sh.skel.md). **The other CI checks locally**: the commands for the fuzz build and a fuzzer run (copying the corpus first unless its finds are meant to be kept; a fixed crash goes into `fuzz/corpus/<target>/` as `regression-<name>`), clang-tidy, [tools/ci/valgrind.sh](./tools/ci/valgrind.sh.skel.md) over a build configured with `-DPCRE2_SUPPORT_VALGRIND=ON`, and the [stress tests](./tests/stress_test.cpp.skel.md) (`ctest --preset linux-release-stress`; about 10 GB of disk; `MOD_STRESS_DIR` and `MOD_STRESS_BYTES`). **Building on Windows and macOS**: the MSYS2 MSYS shell (not UCRT64 or MINGW64) with gcc, cmake, ninja and zip, the `windows-release` preset and `tools/ci/package_windows.sh`; Homebrew's llvm, ninja and cmake with the `macos-release` preset and Homebrew's `clang++` (macOS's own libc++), and `tools/ci/package_macos.sh` to join two architectures; `tools/ci/smoke.py` for any build.

- **Required:** optional.
- **Failure modes:** none.
- **Depends on:** none
- **Referred by:** none known (read by people)
- **Unknowns:** none
