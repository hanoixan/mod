---
role: product
stamp: source 58504049, stand-in 1bd51ce3
---
# resource: help.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: the help viewer (F1, Help > Documentation; pages laid out for reading, links as their text; a table of its Lynx-style keys; F1 or Esc returns to the untouched document; the same page next time; web links not followed; menus and option editors work, document commands close the help first), searching the manual (`/` or Ctrl+F, as you type, `page:line`, Enter, Left back, kept for the next time), where the manual is looked for, in order, as [find_doc_dir](../../src/app/doc_search.hpp.skel.md#function-find_doc_dir) does, and Help > About mod.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** [find_doc_dir](../../src/app/doc_search.hpp.skel.md#function-find_doc_dir)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
