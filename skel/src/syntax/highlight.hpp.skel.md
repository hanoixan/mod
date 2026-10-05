---
role: product
stamp: source d8613c5a, stand-in 94a4513c
---
# module: highlight

The common vocabulary for anything that colors text. Highlighters produce **styles**, which are semantic classes; the [theme](../ui/theme.hpp.skel.md) turns styles into terminal attributes. Each document gets at most one active highlighter:

- [MarkdownHighlighter](./markdown.hpp.skel.md#class-markdownhighlighter) for `.md` and `.markdown`.
- Otherwise, for a file whose [language entry](./language_config.hpp.skel.md#symbol-languageserverspec) matches: a [SyntaxHighlighter](./syntax_highlighter.hpp.skel.md#class-syntaxhighlighter) when the entry has a `syntax` description, and a [SemanticHighlighter](./semantic_highlighter.hpp.skel.md#class-semantichighlighter) when it has a server `command` and the file is under the size cap. With both, a [LayeredHighlighter](./layered_highlighter.hpp.skel.md#class-layeredhighlighter) draws the server's tokens over the syntax layer; with one, that one is used alone.
- Otherwise none.

- **Owns:** the `Style`, `StyleSpan` and `Highlighter` declarations.
- **Access:** public, header-only.
- **Required:** always.
- **Failure modes:** none at runtime. Highlighters must never throw into the render path; failures degrade to no spans.
- **Depends on:** [DocumentListener](../edit/document.hpp.skel.md#symbol-documentlistener)
- **Unknowns:** none

## symbol: Style

`enum class Style : uint8_t`. The values are:

- `Default`, spelled as written (the one enumerator that is not snake_case).
- Markdown: `md_heading1` … `md_heading6`, `md_emphasis` (italic), `md_strong` (bold), `md_strike`, `md_code`, `md_code_block`, `md_link_text`, `md_link_url`, `md_quote`, `md_list_marker`, `md_markup` (the dimmed `*`, `#`, backtick and similar characters).
- LSP standard token types, each with an `lsp_` prefix (parallel to `md_`) and in snake_case, because several names are C++ keywords: `lsp_namespace`, `lsp_type`, `lsp_class`, `lsp_enum`, `lsp_interface`, `lsp_struct`, `lsp_type_parameter`, `lsp_parameter`, `lsp_variable`, `lsp_property`, `lsp_enum_member`, `lsp_event`, `lsp_function`, `lsp_method`, `lsp_macro`, `lsp_keyword`, `lsp_modifier`, `lsp_comment`, `lsp_string`, `lsp_number`, `lsp_regexp`, `lsp_operator`, `lsp_decorator`. They map from the LSP names `namespace`, `type`, `class`, `enum`, `interface`, `struct`, `typeParameter`, `parameter`, `variable`, `property`, `enumMember`, `event`, `function`, `method`, `macro`, `keyword`, `modifier`, `comment`, `string`, `number`, `regexp`, `operator`, `decorator`.
- `constant`: a literal constant named by a word (`True`, `None`, `nullptr`, `false`), from the syntax layer.
- `page`: the text area's background, set by the theme's darkness.
- `history_inserted`, `history_removed`: the Undo History pane's preview of a step's change.
- UI: `search_match`, `selection`, `gutter`, `gutter_current`, `status`, `menu`, `menu_selected`, `menu_accel`, `error`, `history_read_only` (dimmed read-only rows in the undo-history panel), `overflow_marker` (the `>` on a row whose line runs past the right edge).

- **Access:** public.
- **Referred by:** [theme](../ui/theme.hpp.skel.md)
- **Referred by:** [markdown_render](./markdown_render.hpp.skel.md)

## symbol: StyleSpan

`{ uint64_t start; uint64_t end; Style style; uint8_t modifiers; }`.

- Offsets are absolute byte offsets, half-open `[start, end)`.
- `modifiers` is a bit set for the LSP modifiers that the theme renders, with the constants `kModDeprecated = 1`, `kModReadonly = 2`, `kModDocumentation = 4`, `kModDefaultLibrary = 8` and `kModDeclaration = 16`. How each looks is the [color theme](../ui/theme.hpp.skel.md)'s business.
- Spans for one line are sorted and do not overlap. Markdown nesting is flattened by the highlighter.

- **Access:** public.
- **Referred by:** [markdown_test](../../tests/markdown_test.cpp.skel.md)

## class: Highlighter

An abstract interface that extends `DocumentListener`. A highlighter takes a `const Document&`, so it cannot register itself: its owner (App, or a test fixture) calls `Document.add_listener` after creating it and `remove_listener` before destroying it.

- **Inputs:** none (abstract).
- **State changes:** implementation-defined caches, invalidated through `after_change`.
- **Owns:** its caches.
- **Access:** main thread. Called by [EditorView.render](../ui/editor_view.hpp.skel.md#function-render) for each visible line.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [markdown](./markdown.hpp.skel.md)
- **Referred by:** [semantic_highlighter](./semantic_highlighter.hpp.skel.md)
- **Referred by:** [document_slot](../app/document_slot.hpp.skel.md)
- **Referred by:** [layered_highlighter](./layered_highlighter.hpp.skel.md)
- **Referred by:** [syntax_highlighter](./syntax_highlighter.hpp.skel.md)

### function: spans_for_line

- **Inputs:** `line_start`: an offset; `line_bytes`: a `std::string_view` of the line's bytes without the LF (a CR before it may be included), possibly truncated at the end to the visible window plus some margin. Truncation only ever drops the end of the line.
- **Returns:** `std::vector<StyleSpan>`. An empty vector means the default style everywhere. It must return quickly from cached or locally computed data and never block on I/O.
- **State changes:** may fill caches.
- **Access:** EditorView.
- **Referred by:** [editor_view](../ui/editor_view.hpp.skel.md)

### function: visible_range_changed

- **Inputs:** `first_offset`, `last_offset`.
- **Returns:** nothing.
- **State changes:** lets asynchronous highlighters prefetch, for example by requesting LSP range tokens. The default does nothing.
- **Access:** EditorView, after scrolling.

### function: tick

- **Inputs:** `now`: a `std::chrono::steady_clock::time_point`.
- **Returns:** `std::optional<time_point>`: the next deadline, or `nullopt` when no timer is armed. The default returns `nullopt`.
- **State changes:** runs the highlighter's timers (debounced requests, LSP timeouts). It is virtual so that App can call it on whichever highlighter is active.
- **Access:** [App.run](../app/app.hpp.skel.md#function-run), once per loop iteration; App folds the deadline into its wait timeout.

### function: status

- **Inputs:** none.
- **Returns:** a short `std::string` for the status line's LSP field ("LSP: cpp", "LSP off: …"), or empty. The default returns empty.
- **State changes:** none.
- **Access:** [EditorView.render](../ui/editor_view.hpp.skel.md#function-render), through App.
