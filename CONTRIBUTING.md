# Contributing

## GitHub Flow

`main` is always releasable. All work reaches it through a pull request:

1. Branch off `main` (`git switch -c my-change main`).
2. Commit and push the branch; open a pull request against `main`.
3. Wait for CI to pass. It builds the release configuration, runs the tests, builds the `.deb`, `.rpm` and Arch packages and the tarball, and installs and runs each one on its own distribution (the tarball on Debian 12). The packages are attached to the run for seven days. Alongside, it fuzzes every parser for 60 seconds each from the corpus in `fuzz/corpus/`, runs `clang-tidy` with the checks in `.clang-tidy` (any finding fails), runs every test under valgrind memcheck, and runs 1 GB and 4 GB text files and a 1 GB binary file through the editor (the `stress` tests).
4. Merge the pull request, then delete the branch.

Branch protection is not enforced: GitHub only offers it on public repositories or on paid plans. The rule above is by agreement until it can be turned on under Settings > Branches (require a pull request and the CI check before merging).

## Releasing

A release starts as a release candidate, made from `main`, which is promoted once it has been tried. Promotion builds nothing: the release is the candidate's files, byte for byte.

1. In a pull request, set the new version in `CMakeLists.txt` (`project(mod VERSION 1.2.0 …)`), and merge it.
2. In GitHub, open Actions > Release candidate > Run workflow, keep the branch on `main`, and enter the same version. The workflow checks that it runs on `main`, that the version matches `CMakeLists.txt` and is not released yet, builds and tests, packages, tags `v1.2.0-rc.1` (`rc.2` for the next candidate of that version, and so on) and publishes it as a GitHub prerelease, "mod 1.2.0 release candidate 1".
3. Try it. `MOD_VERSION=1.2.0-rc.1` points the install one-liner at a candidate; without it the one-liner installs the latest release and never a candidate. To fix something, merge the fix and run step 2 again for the next candidate.
4. Open Actions > Promote release > Run workflow, enter the candidate's tag (`v1.2.0-rc.2`) and type its version (`1.2.0`) to confirm. The workflow tags `v1.2.0` on the candidate's commit and publishes the candidate's files as the release "mod 1.2.0", with notes on every change since the previous release. It runs in the `release` environment; GitHub offers a required reviewer there only on a public repository or an Enterprise plan, so until then starting it is the approval.

A candidate and its release carry:

- `mod_<version>_amd64.deb` for Debian and Ubuntu (22.04 or later),
- `mod-<version>-1.x86_64.rpm` for Fedora and other RPM distributions,
- `mod-<version>-1-x86_64.pkg.tar.zst` for Arch (`pacman -U`),
- `mod-<version>-linux-x86_64.tar.gz`, the same files to unpack anywhere; `install.sh` uses it when it cannot use a package manager,
- `PKGBUILD` and `mod-<version>.tar.gz`, to build the Arch package from source with `makepkg`.

The files carry the plain version, never `rc`: that is what lets the release be the very files tried. So `mod --version` on a candidate says `1.2.0`, and its `PKGBUILD` downloads its source from the release, which works once the candidate is promoted (the `.pkg.tar.zst` works on a candidate).

If a run fails after its tag was pushed, delete the tag (`git push origin :refs/tags/<tag>`) before running it again. `tools/ci/release.sh` holds the checks and names both workflows use; `tools/ci/release_test.sh` tests it, and CI runs it.

## The README's pictures

`python3 tools/screenshot.py` runs the release build in a pseudo-terminal and draws `docs/images/screenshot.png` and `docs/images/undo-history.png` (it needs `pip install pyte pillow` and the DejaVu Sans Mono font). Run it again when the look changes.

## Building the packages locally

`tools/ci/package.sh` is what CI runs. On Ubuntu 22.04 or later it needs `g++-13`, `cmake` 3.25 or later, `ninja-build`, `rpm` and Docker; it leaves the packages and the tarball in `dist/`. `MOD_SKIP_PACKAGE_TESTS=1` skips the install checks.

To run the other checks locally (clang 19 and libstdc++ 13 for the first two):

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMOD_BUILD_FUZZERS=ON -DMOD_BUILD_TESTS=OFF
cmake --build build/fuzz && build/fuzz/fuzz/fuzz_json fuzz/corpus/json -max_total_time=60
cmake -S . -B build/tidy -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DMOD_BUILD_TESTS=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
find src -name '*.cpp' | xargs clang-tidy-19 -p build/tidy --quiet
cmake -S . -B build/valgrind -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPCRE2_SUPPORT_VALGRIND=ON
cmake --build build/valgrind && tools/ci/valgrind.sh build/valgrind
```

The stress tests need about 10 GB of free disk (`MOD_STRESS_DIR` puts the files elsewhere; `MOD_STRESS_BYTES=50000000` makes every file 50 MB for a quick run):

```sh
cmake --preset linux-release && cmake --build --preset linux-release
ctest --preset linux-release-stress
```

A fuzzer run writes into the corpus folder it is given; copy it first unless you mean to keep what it finds. A crash input that is fixed goes into `fuzz/corpus/<target>/` as `regression-<name>`, so ctest replays it.
