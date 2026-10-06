---
role: product
untested: an installer run by people; checked by hand on Windows against a release
stamp: source 5134483a, stand-in a8b85f66
---
# module: install.ps1

The Windows installer the README offers: `irm https://raw.githubusercontent.com/hanoixan/mod/main/install.ps1 | iex`, in PowerShell. It stops on any error (`$ErrorActionPreference = 'Stop'`) and never calls `exit`, which would close the user's shell when run through `iex`.

1. Finds the release as [install.sh](./install.sh.skel.md) does: `$env:MOD_VERSION` (a release, or a candidate such as `1.2.0-rc.1`), else the latest release's tag from the GitHub API; the file is named with the plain version.
2. Downloads `mod-<version>-windows-x86_64.zip` ([package_windows.sh](./tools/ci/package_windows.sh.skel.md)) into a temporary folder, unpacks it, and copies its tree into `$env:MOD_PREFIX` (default `%LOCALAPPDATA%\Programs\mod`), replacing an earlier install.
3. Adds `<prefix>\bin` to the user's `Path` if it is not there, and says to open a new terminal.

`$env:MOD_RELEASES` and `$env:MOD_API` override the URLs, for testing.

- **Owns:** a temporary download folder.
- **Access:** people, through PowerShell.
- **Required:** optional.
- **Failure modes:** no network, no release, a failed download or unpack: each stops with PowerShell's error, the temporary folder removed.
- **Depends on:** [package_windows.sh](./tools/ci/package_windows.sh.skel.md)
- **Referred by:** [README.md](./README.md.skel.md)
- **Unknowns:** none
