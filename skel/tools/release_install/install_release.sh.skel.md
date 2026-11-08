---
role: product
untested: a developer tool; checked by hand against published releases on WSL, Linux and macOS
stamp: source 3c1e446d, stand-in 93c3136e
---
# module: install_release.sh

Installs a release already published on GitHub on the Linux x86_64 or macOS machine it runs on: `install_release.sh [--system] [--prefix DIR] VERSION`. POSIX `sh`, `set -eu`. For developers: unlike [install.sh](../../install.sh.skel.md), the README's one-liner, it never chooses the version (it is required) or the kind of install (per user unless `--system`), and it shares no code with it.

1. Checks `VERSION` is a release's version as tagged without the `v`: `X.Y.Z` or a candidate `X.Y.Z-rc.N`. The files are downloaded from tag `vVERSION` and are named with the plain version (`X.Y.Z`), a candidate's too ([package.sh](../ci/package.sh.skel.md)).
2. By default, downloads the `linux-x86_64` or `macos-universal` tarball and copies it into `DIR` (default `~/.local`), with no sudo, and says so if `DIR/bin` is not on `PATH`.
3. With `--system`, on Linux only, downloads the `.deb`, `.rpm` or Arch package and installs it with apt-get (allowing a downgrade), dnf or pacman, through `sudo` unless already root; sudo asks for the password on the terminal.
4. Runs the installed `mod --version` and fails unless it names the plain version.

`MOD_RELEASES` (default `https://github.com/hanoixan/mod/releases`) overrides the download URL, for testing.

- **Owns:** a temporary download folder, removed on exit.
- **Access:** developers, by hand or through [install_release_targets.sh](./install_release_targets.sh.skel.md).
- **Required:** optional.
- **Failure modes:** a version not in that form, a missing file or release (curl's 404), no curl, an unsupported system or architecture, `--system` on macOS, with `--prefix`, without sudo or without a supported package manager, and an installed mod that reports another version: each stops with a message and a non-zero exit.
- **Depends on:** [package.sh](../ci/package.sh.skel.md)
- **Unknowns:** none
- **Referred by:** [install_release_targets.sh](./install_release_targets.sh.skel.md)
