---
role: product
untested: an installer run by people; checked by hand in containers against a local copy of a release
stamp: source 37f26110, stand-in b6a0c882
---
# module: install.sh

The one-line installer the README offers: `curl -fsSL https://raw.githubusercontent.com/hanoixan/mod/main/install.sh | sh`. POSIX `sh`, `set -eu`, Linux x86_64 only (anything else stops with a message).

1. Finds the release: `MOD_VERSION` if set (a release such as `1.2.0`, or a release candidate such as `1.2.0-rc.1`), else the latest release's tag from the GitHub API, which never names a candidate (a prerelease). The files are downloaded from that tag, and named with the plain version (`1.2.0`) either way.
2. With apt-get, dnf or pacman, and root or `sudo`, downloads that release's `.deb`, `.rpm` or Arch package ([package.sh](./tools/ci/package.sh.skel.md)) and installs it with the package manager, so it is updated and removed the usual way.
3. Otherwise, or with `MOD_INSTALL_LOCAL=1`, downloads the `linux-x86_64` tarball and unpacks it into `MOD_PREFIX` (default `~/.local`), and says so if `$MOD_PREFIX/bin` is not on `PATH`.

It prints what it does and exits non-zero on any failure, leaving nothing half-installed outside its temporary folder (removed on exit). `MOD_RELEASES` (default `https://github.com/hanoixan/mod/releases`) and `MOD_API` override the URLs, for testing against a local copy.

- **Owns:** a temporary download folder.
- **Access:** people, through curl.
- **Required:** optional.
- **Failure modes:** no curl; no network; a private repository (the downloads need access, so the one-liner only works once the repository is public); an unsupported system. Each stops with a message.
- **Depends on:** [package.sh](./tools/ci/package.sh.skel.md)
- **Referred by:** [README.md](./README.md.skel.md)
- **Unknowns:** none
