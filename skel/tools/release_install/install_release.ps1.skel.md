---
role: product
untested: a developer tool; checked by hand against published releases on Windows 11
stamp: source 4bcd9a8a, stand-in d42fa32d
---
# module: install_release.ps1

Installs a release already published on GitHub on the Windows machine it runs on, for its user: `powershell -ExecutionPolicy Bypass -File install_release.ps1 -Version VERSION [-Prefix DIR]`. Windows PowerShell 5.1. For developers: unlike [install.ps1](../../install.ps1.skel.md), the README's one-liner, the version is required, and it shares no code with it. There is no system install on Windows.

1. Checks `-Version` is `X.Y.Z` or `X.Y.Z-rc.N`; the zip is downloaded from tag `vVERSION` and named with the plain version ([package_windows.sh](../ci/package_windows.sh.skel.md)).
2. Unpacks `mod-<plain>-windows-x86_64.zip` into `-Prefix` (default `%LOCALAPPDATA%\Programs\mod`).
3. Runs the installed `mod.exe --version` and fails unless it names the plain version.
4. Adds `-Prefix\bin` to the user `PATH` if it is not there, and says a new terminal is needed.

Errors are one line on standard error and exit 1 (no PowerShell error record). `MOD_RELEASES` overrides the download URL.

- **Owns:** a temporary download folder, removed on exit; the user `PATH` entry.
- **Access:** developers, by hand or through [install_release_targets.sh](./install_release_targets.sh.skel.md).
- **Required:** optional.
- **Failure modes:** a version not in that form, a missing release or file (the 404 is named), `mod.exe` in use while it is replaced, and an installed mod that reports another version: each stops with a message and exit 1.
- **Depends on:** [package_windows.sh](../ci/package_windows.sh.skel.md)
- **Unknowns:** none
- **Referred by:** [install_release_targets.sh](./install_release_targets.sh.skel.md)
