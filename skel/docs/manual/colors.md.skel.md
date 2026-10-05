---
role: product
stamp: source e30d0cf3, stand-in 93b02b3e
---
# resource: colors.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: the Colors editor (keys, samples, `*`), color specs (every word, the 16-color rule and why), a table of **every** color name with what it colors and its default, the modifiers and how they combine, darkness and its defaults per level (normal, night, paper; vt100's fallbacks), the `colors` member of settings.json with an example, and a note that the terminal's own palette decides the actual shades.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) checks that it names every color name in backticks.
- **Depends on:** [color_names](../../src/ui/theme.hpp.skel.md#function-color_names)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)
