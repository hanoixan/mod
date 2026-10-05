---
role: product
kind: resource
stamp: source 49e191d3, stand-in 59275b4c
---
# resource: README.md

The repository's front page. Contents, in order: one line on what mod is; [screenshot.png](./docs/images/screenshot.png.skel.md); install (the [install.sh](./install.sh.skel.md) one-liner, noting it needs the repository to be public; the release's `.deb`, `.rpm`, Arch package and tarball; building from source); a quickstart (opening files, Esc for the menu, saving, quitting, F1 for the manual, the command-line options); the features, persistent, branched, unlimited undo first, with [undo-history.png](./docs/images/undo-history.png.skel.md); a cheat sheet of the default keys most used, from the keymap, linking to the full list in the manual; documentation (links to the manual and CONTRIBUTING); the license (MIT).

- **Required:** optional.
- **Failure modes:** it drifts from the program; the documentation audit checks it.
- **Depends on:** [install.sh](./install.sh.skel.md)
- **Depends on:** [screenshot.png](./docs/images/screenshot.png.skel.md)
- **Depends on:** [undo-history.png](./docs/images/undo-history.png.skel.md)
- **Referred by:** none known (the repository front page)
- **Unknowns:** none
