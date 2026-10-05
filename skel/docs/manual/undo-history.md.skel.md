---
role: product
stamp: source b8adf975, stand-in cbf84ad2
---
# resource: undo-history.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: The branching history, the `.history` file and why it holds deleted text, the Undo History pane (markers, keys) and its two commands, Clear History… (C) and Trim History… (T), with what each keeps and that both are permanent; the slow-load prune offer; also Persist History (off by default: history in memory only; P; a loadable `.history` turns it on; turning it on writes the whole session's history; off keeps the file; an unreadable file is overwritten after a question; untitled until saved) and Trim History (T, formerly Prune); and the preview beside the pane (the selected state shown read-only at the same lines, inserted text highlighted and removed text struck through, Tab between the pane and the text, Enter jumps, Esc changes nothing).

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
