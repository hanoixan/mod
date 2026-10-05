---
role: product
stamp: source dbc9b31b, stand-in 718c90e3
---
# resource: command-line.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: the synopsis; `-ro`/`--read-only` (the named files open read-only; later ones as usual); `--persist-history` (the history file is written from the first change; an unreadable one is asked about); `--`; the session-only setting options generated from the schema (number, on/off with `--no-`, choices with dashes, `--color NAME=SPEC`), never saved, replaced by a change in the session; bad options stop mod with a message; examples.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** [parse_cli](../../src/app/cli_options.hpp.skel.md#function-parse_cli)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)
