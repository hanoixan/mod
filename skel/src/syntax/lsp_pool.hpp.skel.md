---
role: product
stamp: source de541aa7, stand-in cf9dfca5
---
# module: lsp_pool

One language server per language and project, shared by every open document that needs it. With several documents open, five C++ files of one project run one clangd, not five. A server is identified by the language spec's `id` and the project root from [find_project_root](./language_config.hpp.skel.md#function-find_project_root); a file of the same language in another project gets a server of its own.

The pool holds only weak references: a server lives while some [SemanticHighlighter](./semantic_highlighter.hpp.skel.md#class-semantichighlighter) holds it, and when the last one lets go, the [LspClient](./lsp_client.hpp.skel.md#class-lspclient) is destroyed, which shuts the server down. Opening a document of that language again later starts a fresh server.

- **Owns:** the map from (language, root) to the server in use.
- **Access:** public. One instance, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** optional — as for LspClient.
- **Failure modes:** a server that fails to start stays in the pool while documents hold it, so its other documents show the same "LSP off: <reason>" without spawning it again; the next acquire after all of them are gone tries again.
- **Depends on:** [LspClient](./lsp_client.hpp.skel.md#class-lspclient)
- **Depends on:** [LanguageServerSpec](./language_config.hpp.skel.md#symbol-languageserverspec)
- **Unknowns:** none

## class: LspServerPool

- **Inputs:** `queue`: the `EventQueue&` every client posts to.
- **State changes:** entries whose server nobody holds are dropped on the next `acquire`.
- **Owns:** see the module.
- **Access:** App owns it; SemanticHighlighter acquires from it.
- **Referred by:** [lsp_client_test](../../tests/lsp_client_test.cpp.skel.md)
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [lsp_pool (implementation)](./lsp_pool.cpp.skel.md)

### function: acquire

- **Inputs:** `spec`; `root`: a project root.
- **Returns:** `std::shared_ptr<LspClient>`: the live server for `(spec.id, root)`, or a new one, already started with [LspClient.start](./lsp_client.hpp.skel.md#function-start) (a start failure shows in its status text).
- **State changes:** records a new server.
- **Access:** SemanticHighlighter.
- **Referred by:** [semantic_highlighter](./semantic_highlighter.hpp.skel.md)

### function: live_servers

- **Inputs:** none.
- **Returns:** how many servers someone still holds.
- **State changes:** none.
- **Access:** tests.
