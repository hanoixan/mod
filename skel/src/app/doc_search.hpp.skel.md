---
role: product
stamp: source 7d8b1792, stand-in 38428528
---
# module: doc_search

The help's two needs that touch files: finding the manual, and searching all of it.

**Finding the manual.** The folder that holds the manual's `index.md` is the first of these that does: `$MOD_DOC_DIR`; `<binary's folder>/../share/mod/doc`, which is where `cmake --install` puts it relative to `bin/mod` and so survives moving a whole install; the install path compiled in (`MOD_INSTALL_DOC_DIR`); the source tree's `docs/manual` (`MOD_SOURCE_DOC_DIR`), so a binary run from its build folder finds the manual it was built with. See [installed_data](../../infra/storage.iac.skel.md#resource-installed_data).

**Searching.** Plain text (no pattern syntax), ignoring ASCII case, over every page, read whole each time: the manual is a few dozen small files, so there is no index.

- **Owns:** nothing.
- **Access:** public. Main thread.
- **Required:** optional — without it there is no help screen.
- **Failure modes:** no candidate holds `index.md`: App shows where it looked. An unreadable page is searched as empty.
- **Depends on:** [installed_data](../../infra/storage.iac.skel.md#resource-installed_data)
- **Unknowns:** none
- **Referred by:** [doc_search (implementation)](./doc_search.cpp.skel.md)

## symbol: DocMatch

`{ std::filesystem::path page; uint64_t offset; uint64_t line; uint64_t column; std::string excerpt; }`: a match. `page` is relative to the manual's folder; `offset` is in bytes from the page's start; `line` and `column` are 1-based (the column in bytes); `excerpt` is the matching line without leading or trailing blanks, at most 200 bytes. `DocSearchResult` is `{ std::vector<DocMatch> matches; bool capped; }`, and `kMaxDocMatches` is 500.

- **Access:** public.
- **Referred by:** [doc_search_view](../ui/doc_search_view.hpp.skel.md)

## symbol: DocDirSources

`{ optional<path> env; optional<path> exe; path installed; path source; }`: the inputs of the lookup, so tests can give their own. `doc_dir_sources()` fills them for this process: `MOD_DOC_DIR` when set and not empty, the binary's path (`/proc/self/exe` on Linux, `_NSGetExecutablePath` on macOS), and the two compiled-in folders.

- **Access:** public.

## function: doc_pages

- **Inputs:** `root`: the manual's folder.
- **Returns:** every `.md` file under it, recursively, relative to it: `index.md` first, the rest in path order.
- **State changes:** none.
- **Access:** `search_docs` and tests.

## function: search_docs

- **Inputs:** `root`; `query`; `cap`: the most matches to return, 500 by default.
- **Returns:** every occurrence of `query` in every page, ignoring ASCII case, in page order then position (occurrences do not overlap); `capped` when there were more than `cap`. An empty query returns nothing.
- **State changes:** none; it reads the pages.
- **Access:** App, through the search panel; tests.
- **Referred by:** [doc_search_test](../../tests/doc_search_test.cpp.skel.md)

## function: find_doc_dir

- **Inputs:** a `DocDirSources`.
- **Returns:** the first candidate folder holding `index.md`, or `nullopt`. `doc_dir_candidates` returns the candidates in order (absent and empty sources left out), for the message when none is found.
- **State changes:** none.
- **Access:** App, the first time F1 is pressed; tests.
- **Referred by:** [help.md](../../docs/manual/help.md.skel.md)
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [doc_search_test](../../tests/doc_search_test.cpp.skel.md)
