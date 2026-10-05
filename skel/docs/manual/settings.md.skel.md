---
role: product
stamp: source 1b293f66, stand-in 787f2389
---
# resource: settings.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: The User Settings panel's keys, the recent settings in Options, a table of **every** setting of the schema with its key in backticks, default and meaning; View toggles versus starting values; `settings.json`: location, hand editing, bad values, `modified`, unknown members, the installed reference copy (not read by mod); an example; also the `colors` row, the `terminal_mode` choice (applies when mod next starts), `syntax_coloring` covering Markdown (there is no separate Markdown setting), the `darkness` choice, the `cursor_style` choice, and the `tab_inserts` choice (and word wrap on by default); also a pointer to the command line's session-only settings.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** [setting_specs](../../src/app/settings.hpp.skel.md#function-setting_specs)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)
