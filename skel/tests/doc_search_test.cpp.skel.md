---
role: test
stamp: source 1807ceee, stand-in cbda6e77
---
# module: doc_search_test

Over a small manual written to a scratch folder: the pages are every `.md` under the root with `index.md` first, others in path order and other files left out; search finds every match in every page ignoring case, with line, column, offset and the trimmed line, finds nothing for an empty or absent query, and treats `+` as text; the cap stops the search and marks the result; the manual is found in `$MOD_DOC_DIR`, then beside the binary, then the install path, then the source tree, a candidate without `index.md` is skipped, none at all gives `nullopt`, and the candidates are listed in order. The panel: typing searches, rows read `page:line  text`, Down and Enter return the selected match and close; Esc closes, reopening keeps the query, results and selection without searching again, Ctrl+Backspace clears and a paste stops at its line break; rendering draws the field, the count, the highlighted rows and "no matches", nothing outside the area.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [search_docs](../src/app/doc_search.hpp.skel.md#function-search_docs)
- **Depends on:** [find_doc_dir](../src/app/doc_search.hpp.skel.md#function-find_doc_dir)
- **Depends on:** [DocSearchView](../src/ui/doc_search_view.hpp.skel.md#class-docsearchview)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
