---
role: product
stamp: source 10265b3c, stand-in a496e3e3
---
# module: semantic_highlighter

A [Highlighter](./highlight.hpp.skel.md#class-highlighter) backed by LSP semantic tokens. It converts document changes into `didChange` notifications, requests tokens for the visible range (or the full document if the server lacks range support), decodes the relative token encoding into absolute byte spans, and caches them.

It keeps its **own line-start index** of the document, a sorted vector of the offsets after each LF, built by one streaming pass over the text at construction and on `reloaded`, and updated on every change (starts inside the removed text are dropped, later ones shifted by the length delta, and the inserted text's LFs added). LSP positions need line numbers both ways, and [PieceTree.line_of](../text/piece_tree.hpp.skel.md#function-line_of) and [line_start](../text/piece_tree.hpp.skel.md#function-line_start) cannot be used: forcing them mutates the tree, which the highlighter only sees through a `const Document&`, and unforced they fail until the background scan finishes. The document is under the LSP size cap, so the index is small (8 bytes per line) and the build pass is bounded.

- **Owns:** its document's registration with a shared [LspClient](./lsp_client.hpp.skel.md#class-lspclient), the line index, the token cache, and the debounce timer.
- **Access:** public. Created by App when [LanguageConfig.find_for_path](./language_config.hpp.skel.md#function-find_for_path) matches and the document is under the size cap, and registered with `Document.add_listener` by App.
- **Required:** optional — as for LspClient.
- **Failure modes:** tokens arrive for an old version, so they are dropped and re-requested after the debounce. Between an edit and fresh tokens, cached spans before the edit are kept, spans after it are shifted by the byte delta, a span the edit lands inside keeps covering the typed text (unless the inserted text holds an LF, in which case it ends at the edit), and the parts of spans inside removed text disappear, so there is no flicker. A server token legend type that is unknown maps to `Style::Default` and is not cached. A document over the spec's `max_file_bytes` at construction gets no client and the status "LSP off: file too large".
- **Depends on:** [Highlighter](./highlight.hpp.skel.md#class-highlighter)
- **Depends on:** [LspClient](./lsp_client.hpp.skel.md#class-lspclient)
- **Depends on:** [PieceTree.read](../text/piece_tree.hpp.skel.md#function-read)
- **Depends on:** [LspServerPool.acquire](./lsp_pool.hpp.skel.md#function-acquire)
- **Depends on:** [find_project_root](./language_config.hpp.skel.md#function-find_project_root)
- **Unknowns:** none

## class: SemanticHighlighter

- **Inputs:** `doc`: a `const Document&`; `spec`; `servers`: the [LspServerPool](./lsp_pool.hpp.skel.md#class-lspserverpool). The constructor acquires the server for `spec` and the document's [project root](./language_config.hpp.skel.md#function-find_project_root), and registers the document with [open_document](./lsp_client.hpp.skel.md#function-open_document)`(file_uri(path), spec.id, doc.text(), …)`; every later call names the returned `DocumentId`.
- **State changes:** cache: one sorted, non-overlapping vector of absolute `StyleSpan`s for the last received version. A full response replaces it; a range response replaces only the spans in the requested lines' byte range (tokens a server returns outside it are dropped). The debounce is 150 ms after the last edit, scroll or stale response, armed by the next `tick`. The destructor closes its document with [close_document](./lsp_client.hpp.skel.md#function-close_document) and lets go of the server, which shuts down when no other document holds it. `tick` also ticks the shared client; several highlighters ticking it in one loop is harmless.
- **Owns:** see the module.
- **Access:** through `Highlighter`, including `tick(now)` from App for the debounce.
- **Referred by:** [app (implementation)](../app/app.cpp.skel.md)
- **Referred by:** [semantic_highlighter (implementation)](./semantic_highlighter.cpp.skel.md)
- **Referred by:** [lsp_client_test](../../tests/lsp_client_test.cpp.skel.md)

### function: spans_for_line

- **Inputs:** as in [Highlighter.spans_for_line](./highlight.hpp.skel.md#function-spans_for_line).
- **Returns:** the cached spans that overlap `[line_start, line_start + line_bytes.size())`, clipped to it. Positions were converted from (line, character) to byte offsets with the negotiated encoding when the tokens arrived, so this is a binary search with no conversion.
- **State changes:** none.
- **Access:** EditorView.

### function: visible_range_changed

- **Inputs:** as in [Highlighter.visible_range_changed](./highlight.hpp.skel.md#function-visible_range_changed).
- **Returns:** nothing.
- **State changes:** when the visible lines leave the last requested range, records a new range (the visible lines plus 100 lines of margin each way) and, if the server supports range requests, arms the debounce.
- **Access:** EditorView.

### function: tick

- **Inputs:** `now`.
- **Returns:** the next deadline (the debounce or the client's), so App can set its wait timeout.
- **State changes:** sends a debounced token request (the recorded range when the server supports ranges, else the full document; the first 200 lines when it supports only ranges and nothing was scrolled yet), and runs [LspClient.tick](./lsp_client.hpp.skel.md#function-tick).
- **Access:** App.run.

### function: status

- **Inputs:** none.
- **Returns:** the client's `status_text()`, or the reason there is no client.
- **State changes:** none.
- **Access:** the status line, through App.
