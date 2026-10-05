---
role: manifest
kind: resource
stamp: source 68d9c5c4, stand-in fa77b984
---
# resource: PKGBUILD.in

The Arch `PKGBUILD` published with each release, for building mod from source with `makepkg`: it downloads the release's source archive (`$url/releases/download/v$pkgver/mod-$pkgver.tar.gz`, made by the release candidate workflow with `git archive --prefix=mod-<version>/`, so its checksum is the workflow's own; promotion publishes the same file), builds with CMake and Ninja (`-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DMOD_BUILD_TESTS=OFF`), and installs into `$pkgdir`, with the LICENSE under `usr/share/licenses/mod`. The repository's copy holds the placeholders `@VERSION@` (in `pkgver`) and `@SHA256@` (in `sha256sums`), which `.github/workflows/release-candidate.yml` replaces with the version and the archive's sha256 (`sed`) for the published `PKGBUILD`. `makedepends=(cmake ninja git gcc)`, `depends=(glibc gcc-libs)`, `license=(MIT)`. The build fetches PCRE2 with FetchContent, so `makepkg` needs the network.

- **Required:** optional — for Arch users building from source.
- **Failure modes:** none beyond the build's own.
- **Depends on:** none
- **Referred by:** [package.sh](../../tools/ci/package.sh.skel.md)
- **Referred by:** none known (the release candidate workflow, `.github/workflows/release-candidate.yml`, fills it in, and promotion publishes it unchanged, so a candidate's `PKGBUILD` finds its source once the candidate is promoted; the skel checker does not read dot-directories)
- **Unknowns:** none
