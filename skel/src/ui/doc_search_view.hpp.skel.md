---
role: product
stamp: source 1a20d731, stand-in 9d9b3342
---
# module: doc_search_view

The help's search panel, opened with Ctrl+F on the help screen. A full-width panel like the Key Bindings editor: the first row reads "Search the manual: <query>" with the number of matches at the right ("500+ matches" when capped); under it one row per match, `page:line  text`, the selected one highlighted and marked `>`, scrolled to stay visible; "type to search every page" before anything is typed and "no matches" when nothing matches.

Typing searches at once, through the search function App gives it ([search_docs](../app/doc_search.hpp.skel.md#function-search_docs)); Backspace deletes a character and Ctrl+Backspace clears the query; a paste adds its text up to the first line break. Up, Down, Home, End, PageUp and PageDown choose a match; Enter returns it and closes the panel; Esc closes it. The query, the matches and the selection are **kept** when the panel closes, and opening it again shows them as they were without searching again, for the session.

- **Owns:** the query, the matches, the selection and the scroll position.
- **Access:** public. One instance, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** optional — as the help.
- **Failure modes:** none.
- **Depends on:** [DocMatch](../app/doc_search.hpp.skel.md#symbol-docmatch)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none

## symbol: DocSearchKeyResult

`{ bool closed; std::optional<DocMatch> open; }`: the panel closed, and, for Enter on a match, the match to show.

- **Access:** public.

## class: DocSearchView

- **Inputs:** none at construction.
- **State changes:** `closed ⇄ open`; the query and results persist across both.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [doc_search_view (implementation)](./doc_search_view.cpp.skel.md)
- **Referred by:** [doc_search_test](../../tests/doc_search_test.cpp.skel.md)

### function: open

- **Inputs:** `search`: a function from a query to a `DocSearchResult`.
- **Returns:** nothing.
- **State changes:** opens the panel with the kept state. `close()` closes it; `is_open()` reports it.
- **Access:** App (Ctrl+F on the help screen).

### function: handle_key

- **Inputs:** a `KeyEvent`.
- **Returns:** a [DocSearchKeyResult](#symbol-docsearchkeyresult).
- **State changes:** as the module describes.
- **Access:** App, while the panel has focus.

### function: handle_paste

- **Inputs:** pasted bytes.
- **Returns:** nothing.
- **State changes:** appends them up to the first line break to the query and searches.
- **Access:** App.

### function: render

- **Inputs:** a `Screen&`; the area inside the frame.
- **Returns:** nothing.
- **State changes:** draws as the module describes, inside the area only, with the cursor at the end of the query.
- **Access:** App.render.

### function: row_text

- **Inputs:** a match index.
- **Returns:** `page:line  text`, with the page in `/` form. `query()`, `results()` and `selected()` expose the rest of the state.
- **State changes:** none.
- **Access:** `render` and tests.
