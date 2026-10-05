---
role: product
kind: resource
stamp: source 8472ad6c, stand-in a909bed6
---
# resource: README.md

The repository's front page. Contents, in order: one line on what mod is; [screenshot.png](./docs/images/screenshot.png.skel.md); install (the [install.sh](./install.sh.skel.md) one-liner for Linux and macOS 13.3+, the [install.ps1](./install.ps1.skel.md) one for Windows 11 in PowerShell, and the releases page as the alternative; the release's `.deb`, `.rpm`, Arch package, Linux tarball, macOS universal tarball and Windows zip (`mod.exe` with `msys-2.0.dll`); building from source); a quickstart (opening files, Esc for the menu, saving, quitting, F1 for the manual, the command-line options); the features, persistent, branched, unlimited undo first, with [undo-history.png](./docs/images/undo-history.png.skel.md); a cheat sheet of the default keys most used, from the keymap, linking to the full list in the manual; documentation (links to the manual and CONTRIBUTING); the license (MIT).

- **Required:** optional.
- **Failure modes:** it drifts from the program; the documentation audit checks it.
- **Depends on:** [install.sh](./install.sh.skel.md)
- **Depends on:** [screenshot.png](./docs/images/screenshot.png.skel.md)
- **Depends on:** [undo-history.png](./docs/images/undo-history.png.skel.md)
- **Depends on:** [install.ps1](./install.ps1.skel.md)
- **Referred by:** none known (the repository front page)
- **Unknowns:** none
