---
role: product
untested: a build script; CI runs it on every push, and it installs and runs each package it makes
stamp: source bbe42d15, stand-in f6c5c316
---
# module: package.sh

Builds, tests and packages mod for Linux; the one script both workflows run, and developers can run by hand. Bash, `set -euo pipefail`, from the repository root, on Ubuntu 22.04 (the `.github/workflows/ci.yml` runner) with `g++-13`, `ninja`, `cmake`, `rpm` and `docker`.

Steps, each stopping the script on failure:

1. Configure the `linux-release` preset into its own build directory, `build/package` (so a developer's `build/linux-release` is untouched), with `CMAKE_INSTALL_PREFIX=/usr`: the binary records where the manual is installed, and the packages install under `/usr`. Build, and run ctest there, without the tests labelled `stress` (the gigabyte files have their own CI job).
2. `cpack -G "DEB;RPM"` in the build directory: `mod_<version>_amd64.deb` and `mod-<version>-1.x86_64.rpm` (see [CMakeLists.txt](../../CMakeLists.txt.skel.md)).
3. Stage an install tree (`cmake --install … --prefix /usr` into a `DESTDIR`) and make the Arch package in an `archlinux` container with [PKGBUILD-bin.in](../../packaging/arch/PKGBUILD-bin.in.skel.md): `mod-<version>-1-x86_64.pkg.tar.zst`.
4. Pack the same install tree as `mod-<version>-linux-x86_64.tar.gz`, one top folder `mod-<version>-linux-x86_64/` holding `bin/mod` and `share/` (for [install.sh](../../install.sh.skel.md)'s `~/.local` install; F1 finds the manual beside the binary).
5. Install each package in a fresh container of its own family (`ubuntu:22.04` with apt, `fedora` with dnf, `archlinux` with pacman), and unpack the tarball in `debian:12`, and run `mod --version`, which must print `mod <version>`.
6. Leave the three packages and the tarball in `dist/`.

The version comes from `project(mod VERSION …)` in CMakeLists.txt. `MOD_SKIP_PACKAGE_TESTS=1` skips step 4.
- **Referred by:** [install.sh](../../install.sh.skel.md)

## Workflows

The GitHub Actions workflows that run this script live in `.github/workflows/`. The skel checker does not read dot-directories, so their contracts are here.

**`ci.yml`, the smoke test.** The CI workflow: the smoke test. On every push to any branch and every pull request, on `ubuntu-22.04`, it installs `g++-13` (from the toolchain PPA if the image lacks it), `ninja-build` and `rpm`, then runs this script: release build, ctest, the three packages, and an install-and-run check of each; then [smoke.py](./smoke.py.skel.md) on the built mod. The packages are uploaded as an artifact kept 7 days. Runs of one branch cancel earlier runs of it (`concurrency`). Permissions: `contents: read`.

**`ci.yml` builds Windows and macOS too.** **windows** (`windows-latest`, the MSYS2 MSYS environment set up by `msys2/setup-msys2` with gcc, cmake, ninja, git, python and zip): the `windows-release` preset, ctest, [package_windows.sh](./package_windows.sh.skel.md), and smoke.py run by Windows' own Python on the zip's `mod.exe` in a Windows pseudo-console (`pywinpty`). **macos** (a matrix of `macos-15`, arm64, and `macos-15-intel`, x86_64; Homebrew `llvm` and `ninja`): the `macos-release` preset with Homebrew's `clang++` (and the system's libc++), ctest, `otool -L` of mod, smoke.py, and an install of the build uploaded as `stage-macos-<arch>`; then **macos-universal** (`macos-15`) joins the two with [package_macos.sh](./package_macos.sh.skel.md) and smoke tests the unpacked universal tarball.

**`ci.yml` also runs**, each in its own job on every push and pull request: **fuzz** (clang 19 from apt.llvm.org over GCC 13's libstdc++, set up by the composite action `.github/actions/clang`; the [fuzz targets](../../fuzz/CMakeLists.txt.skel.md) built with `MOD_BUILD_FUZZERS`, each run 60 s from a copy of its committed corpus; a crash fails the job and its input is uploaded); **clang-tidy** (the checks in `.clang-tidy` over every source file; any finding fails); **valgrind** ([valgrind.sh](./valgrind.sh.skel.md) over a RelWithDebInfo build); and **stress** (the release build's [stress_test](../../tests/stress_test.cpp.skel.md) through the `linux-release-stress` test preset: about 10 GB of disk and two minutes).

**`release-candidate.yml`, a release candidate.** `workflow_dispatch` with one input, `version` (such as `1.2.0`). Permissions: `contents: write`; one release workflow at a time (`concurrency: release`). Its jobs:

1. **prepare** (`ubuntu-22.04`): refuses to run unless the workflow runs on `main` and [release.sh](./release.sh.skel.md) `check-version` accepts the version (`MAJOR.MINOR.PATCH`, equal to CMakeLists.txt, not released yet); numbers the candidate with `next-rc`, and passes the tag, the number and the previous tag to the others.
2. In parallel, after prepare: **linux** runs this script and smoke.py and uploads `dist/`; **windows** builds, tests and packages as `ci.yml` does and smoke tests the zip's `mod.exe` in a Windows pseudo-console; **macos** (arm64 and x86_64) builds, tests, smoke tests and uploads an install of each; then **macos-universal** joins them into `mod-<version>-macos-universal.tar.gz` and smoke tests it.
3. **publish** (`ubuntu-22.04`, after all of them): gathers every package into `dist/`; creates and pushes the annotated tag `v<version>-rc.<n>` on the commit; makes the source archive `mod-<version>.tar.gz` with `git archive --prefix=mod-<version>/` from the tag, and the `PKGBUILD` from [PKGBUILD.in](../../packaging/arch/PKGBUILD.in.skel.md) with the version and that archive's sha256 (both name the release, not the candidate, so they are the release's files too); and publishes the prerelease `v<version>-rc.<n>`, titled "mod <version> release candidate <n>", with the `.deb`, the `.rpm`, the `.pkg.tar.zst`, the Linux tarball, the macOS universal tarball, the Windows zip, the `PKGBUILD` and the source archive, and notes generated since the previous tag (candidate or release). Nothing is tagged or published unless every platform built, tested and passed its smoke test.

**`promote.yml`, promoting a candidate.** `workflow_dispatch` with two inputs: `candidate` (its tag, such as `v1.2.0-rc.2`) and `confirm` (its version typed again). Permissions: `contents: write`; `concurrency: release`; the job runs in the `release` environment (which has a required reviewer: the run waits for the reviewer's approval before it starts). It builds nothing ([release_test.sh](./release_test.sh.skel.md), which CI's linux job runs first, tests the checks both workflows share):

1. Refuses to run unless `candidate` is a candidate's tag (`release.sh rc-of`), `confirm` equals its version, `v<version>` does not exist, and the candidate is a published prerelease.
2. Creates and pushes the annotated tag `v<version>` on the candidate's commit.
3. Downloads the candidate's files (`gh release download`) and publishes them, unchanged, as the release `v<version>` titled "mod <version>", marked latest, with notes generated since the previous release (`release.sh previous-release`), spanning every candidate.

- **Owns:** `dist/` and its build directory.
- **Access:** CI, the release candidate workflow, developers.
- **Required:** always for CI.
- **Failure modes:** a missing tool fails at its first use with the shell's error; docker unavailable fails steps 3 and 4.
- **Depends on:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Depends on:** [PKGBUILD.in](../../packaging/arch/PKGBUILD.in.skel.md)
- **Referred by:** none known (the workflows in `.github/workflows`, and developers)
- **Depends on:** [PKGBUILD-bin.in](../../packaging/arch/PKGBUILD-bin.in.skel.md)
- **Depends on:** [valgrind.sh](./valgrind.sh.skel.md)
- **Depends on:** [release.sh](./release.sh.skel.md)
- **Depends on:** [smoke.py](./smoke.py.skel.md)
- **Depends on:** [package_windows.sh](./package_windows.sh.skel.md)
- **Depends on:** [package_macos.sh](./package_macos.sh.skel.md)
- **Unknowns:** none
