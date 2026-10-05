---
role: manifest
kind: resource
stamp: source ade82108, stand-in b0fe12fc
---
# resource: PKGBUILD-bin.in

An Arch `PKGBUILD` that packages an already-built install tree (the portable build [package.sh](../../tools/ci/package.sh.skel.md) stages) as `mod-<version>-1-x86_64.pkg.tar.zst`. `pkgver` is passed in by the script; `package()` copies the staged `usr/` tree into `$pkgdir`. Fields: `pkgname=mod`, `arch=(x86_64)`, `license=(MIT)`, `url=https://github.com/hanoixan/mod`, `depends=(glibc gcc-libs)`, `options=(!strip !debug)` (the binary is already release-built).

- **Required:** always for releases.
- **Failure modes:** a missing staged tree fails `package()`.
- **Depends on:** none
- **Referred by:** [package.sh](../../tools/ci/package.sh.skel.md)
- **Unknowns:** none
