---
role: product
stamp: source 24819b03, stand-in cf3d7c58
---
# module: lsp_client

A Language Server Protocol client that does only what syntax coloring needs: lifecycle, document synchronization, and semantic tokens. These are the servers VS Code uses for language support. Note that VS Code's *base* coloring comes from TextMate grammars, not from LSP, so semantic tokens from most servers give sparse coloring (identifiers and types; often not keywords, strings or comments). **That sparseness is accepted.** There is no base layer: no built-in lexer and no TextMate grammar support. Text that no token covers is drawn in `Style::Default`.

Framing is JSON-RPC 2.0 over stdio with `Content-Length` headers. A reader thread parses frames and posts them to the main thread. A writer thread drains an outgoing queue so that a slow server can never block the UI.

- **Owns:** the child process, the reader and writer threads, the outgoing queue, pending request ids, server capabilities, the negotiated position encoding, and the document version counter.
- **Access:** public. At most one per document, owned by [SemanticHighlighter](./semantic_highlighter.hpp.skel.md#class-semantichighlighter). Its API is main-thread only.
- **Required:** optional — without it there is no coloring for non-Markdown files.
- **Failure modes:**
  - The server crashes (its stdout closes): restart at most 3 times with backoff (1 s, 4 s, 16 s), then disable for the session with the status "LSP off: <command> keeps exiting". Each restart sends `initialize` and then `didOpen` with the current text. A server that has been running for `kStableRun` (5 minutes, measured by `tick`) has its restarts forgiven, so one that crashes rarely is restarted every time, from the 1 s backoff.
  - The server is not found: `start` fails with `not_found` and LSP is disabled for the session with the spawn error as the status. There is no restart, because the binary will not appear by retrying; a spawn failure during a restart also disables.
  - The server never answers `initialize` within 10 s: disable ("LSP off: <command> did not answer initialize"), killing it at once. An error response to `initialize` disables too, after a graceful shutdown.
  - Giving up on a server ([fail](#function-fail)) never blocks the editor on a server that has stopped reading or answering: those are killed at once; one that still answers (no semantic tokens, an `initialize` error) gets `shutdown`, `exit` and its grace.
  - The server has no `semanticTokensProvider`: disable ("LSP off: <id> has no semantic tokens") after a graceful shutdown.
  - The server sends requests to the client: each gets the answer the protocol expects, so the server neither hangs nor believes something was done: `workspace/configuration` an array of one null per requested item; `window/workDoneProgress/create`, `client/registerCapability`, `client/unregisterCapability` and `window/showMessageRequest` a null result (accepted); `workspace/applyEdit` `{"applied": false}`; anything else the error `-32601` (method not found).
  - The server sends notifications such as `publishDiagnostics`: dropped on the reader thread, never posted to the main thread, so a server flooding notifications costs the editor nothing.
  - The server stops reading its input: once more than `kMaxQueuedBytes` (64 MiB) of messages wait to be written, the client fails with "the server is not reading" instead of growing without bound.
  - The file exceeds `max_file_bytes`: never start; the status reads "LSP off: file too large".
  - The file contains invalid UTF-8: send it with U+FFFD substitution, one per invalid byte. Offsets may then disagree for those lines in the `utf-8` encoding, which is accepted; in `utf-16` each invalid byte and its U+FFFD are both one unit, so they agree.
  - A token request answered with an error (`RequestCancelled`, `ContentModified`): dropped; the next request retries.
- **Depends on:** [ChildProcess](../platform/process.hpp.skel.md#class-childprocess)
- **Depends on:** [Json](./json.hpp.skel.md#class-json)
- **Depends on:** [EventQueue.post](../util/event_queue.hpp.skel.md#function-post)
- **Depends on:** [LanguageServerSpec](./language_config.hpp.skel.md#symbol-languageserverspec)
- **Depends on:** [LSP 3.17 specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)

The size cap is `max_file_bytes` from the language's [LanguageServerSpec](./language_config.hpp.skel.md#symbol-languageserverspec): 4 MiB (4 194 304 bytes) by default, overridable per language with `maxFileBytes` in `languages.json`, either in the embedded defaults or in the user's copy. The comparison is against the document size in bytes when [App.open_document](../app/app.hpp.skel.md#function-open_document) picks the highlighter.

There is no timer thread. Every timeout (initialize, restart backoff, full-sync coalescing) is a deadline run by [tick](#function-tick), which the highlighter calls from App's loop. An operation that arms a timer sets a flag, and the next `tick` sets the deadline from its `now`; so tests drive time by passing `now`, and nothing reads a clock behind their back.

- **Depends on:** [uri_path](../platform/path_text.hpp.skel.md#function-uri_path)
- **Unknowns:** none

## symbol: PositionEncoding

`enum class PositionEncoding { utf8, utf16 }`: the server's `capabilities.positionEncoding`, `utf16` when it is absent or anything other than `"utf-8"`.

- **Access:** public.

## symbol: LspState

`enum class LspState { stopped, starting, initialized, running, failed, shutting_down }`. `starting` is from spawn to the `initialize` response; `initialized` is after it while no document is open; `running` is once `didOpen` was sent. `stopped` is also the state while a restart is pending. `failed` is disabled for the session.

- **Access:** public.

## symbol: LspPosition

`{ uint64_t line; uint64_t character; }`: a 0-based line and a character offset in the negotiated encoding.

- **Access:** public.

## symbol: TokenLegend

`{ std::vector<std::string> types; std::vector<std::string> modifiers; }`, from the server's `semanticTokensProvider.legend`.

- **Access:** public.

## symbol: TokenResponse

`{ int64_t id; uint64_t version; std::optional<LineRange> range; std::vector<uint32_t> data; }`. `LineRange` is `std::pair<uint64_t, uint64_t>`, lines `[first, last)`. `version` is the document version when the request was sent; `range` is the requested range, `nullopt` for a full request; `data` is the server's relative 5-integer encoding, empty for a null result.

- **Access:** public.

## function: file_uri

- **Inputs:** `path`: an absolute path.
- **Returns:** `file://` followed by the path's [uri_path](../platform/path_text.hpp.skel.md#function-uri_path) (on Windows `C:/…`, with a `/` before it), every byte outside the RFC 3986 unreserved set (`A–Z a–z 0–9 - . _ ~`) and `/` percent-encoded with uppercase hex. Used for `rootUri`, the workspace folder and the document URI; servers on Windows are native programs, so they get Windows paths.
- **State changes:** none.
- **Access:** public.
- **Referred by:** [json_test](../../tests/json_test.cpp.skel.md)

## class: FrameParser

The reader thread's frame splitter, public so that framing is unit-tested.

- **Inputs:** none.
- **State changes:** `feed(bytes)` appends to a buffer; `next()` returns the next complete message that parses as a JSON object, or `nullopt` when more bytes are needed. Header names are case-insensitive, other headers (`Content-Type`) are ignored, and the header block ends at `\r\n\r\n`. A header block that is malformed (a line without `name:`, a name with characters other than letters, digits, `-` and `_`, no `Content-Length`, a length over `kMaxBodyBytes` (64 MiB, ample for the tokens of the largest file a server is given), or more than 8 KiB without a terminator) and a body that does not parse are resynchronized: parsing resumes at the next `Content-Length:` (inside the bad body if there is one, which recovers a frame whose length was too long). `resyncs()` counts these; the first and every thousandth are logged, so garbage cannot flood the log.
- **Owns:** the buffer.
- **Access:** the LSP reader thread; tests.
- **Referred by:** [lsp_client (implementation)](./lsp_client.cpp.skel.md)
- **Referred by:** [json_test](../../tests/json_test.cpp.skel.md)
- **Referred by:** [fake_lsp_server](../../tests/fake_lsp_server.cpp.skel.md)
- **Referred by:** [fuzz_lsp_frames](../../fuzz/fuzz_lsp_frames.cpp.skel.md)

## class: LspClient

- **Inputs:** `spec`: a `LanguageServerSpec`; `root_dir`: the project root ([find_project_root](./language_config.hpp.skel.md#function-find_project_root)), sent as `rootUri` and as one `workspaceFolder`; `queue`: an `EventQueue&`. One server serves **any number of documents**, each registered with [open_document](#function-open_document) with its own `const PieceTree&` (read on the main thread whenever its whole text is sent: `didOpen`, also after a restart, and full-text changes) and its own `Callbacks { on_ready, on_tokens }`, called on the main thread: `on_ready()` when the server is initialized and that document's `didOpen` was sent (again after each restart), `on_tokens(TokenResponse)` for each answered token request of that document. A second constructor also takes one `text` and `callbacks`: the **single-document form**, in which `did_open` registers that one document and the calls without a `DocumentId` act on it.
- **State changes:** `stopped → starting → initialized → (running | failed) → shutting_down → stopped`; `running` means initialized with at least one document open, and closing the last document returns to `initialized`. Each document has its own version, which increases on every `did_change` and `did_change_full` and is kept across restarts, its own outstanding token request, and its own full-sync timer.
- **Owns:** see the module.
- **Access:** shared through [LspServerPool](./lsp_pool.hpp.skel.md#class-lspserverpool) by every SemanticHighlighter of one language and project; the single-document form is used by tests.
- **Referred by:** [lsp_client (implementation)](./lsp_client.cpp.skel.md)
- **Referred by:** [semantic_highlighter](./semantic_highlighter.hpp.skel.md)
- **Referred by:** [lsp_client_test](../../tests/lsp_client_test.cpp.skel.md)
- **Referred by:** [lsp_pool](./lsp_pool.hpp.skel.md)

### function: start

- **Inputs:** none.
- **Returns:** `Status`; `not_found` or `process` on a spawn failure, with the state `failed` and the status set.
- **State changes:** spawns the server and sends `initialize` with `processId: null` (no OS call outside `src/platform/`), `clientInfo`, `rootUri`, `workspaceFolders`, the spec's `initializationOptions` when not null, and client capabilities: `general.positionEncodings: ["utf-8","utf-16"]`, `textDocument.semanticTokens { dynamicRegistration: false, requests: { range: true, full: true }, tokenTypes: <all standard>, tokenModifiers: <all standard>, formats: ["relative"], overlappingTokenSupport: false, multilineTokenSupport: false }`, and `textDocument.synchronization { dynamicRegistration: false, didSave: true }`. On the response, it records the legend, the encoding, range and full support, and the sync kind (`change: 2` is incremental; anything else, including `None`, is treated as full), then sends `initialized`, and `didOpen` for every document already registered.
- **Access:** SemanticHighlighter, after the document opens.
- **Depends on:** [ChildProcess.spawn](../platform/process.hpp.skel.md#function-spawn)

### function: open_document

- **Inputs:** `uri`; `language_id` (the spec's `id`); `text`: the document's `const PieceTree&`, which must outlive its registration; `callbacks`.
- **Returns:** a `DocumentId` for the calls below, never 0.
- **State changes:** registers the document. Its `didOpen` (the text streamed from the piece tree into a [JsonStringWriter](./json.hpp.skel.md#class-jsonstringwriter)) is sent at once if the server is initialized, else when it is, and again after every restart; `on_ready` follows. `document_count()` counts the registrations.
- **Access:** SemanticHighlighter, in its constructor.
- **Depends on:** [PieceTree.read](../text/piece_tree.hpp.skel.md#function-read)

### function: close_document

- **Inputs:** a `DocumentId`.
- **Returns:** nothing.
- **State changes:** sends `textDocument/didClose` if the document was opened in the running server, forgets its pending requests (a late answer is ignored), and unregisters it. An unknown id is ignored.
- **Access:** SemanticHighlighter, in its destructor.

### function: ready

- **Inputs:** a `DocumentId`.
- **Returns:** whether the server is running and the document's `didOpen` was sent. `version(id)` returns its version (0 for an unknown id).
- **State changes:** none.
- **Access:** SemanticHighlighter.

### function: did_open

- **Inputs:** `uri`, `language_id` (the spec's `id`). The text is streamed from the piece tree into a [JsonStringWriter](./json.hpp.skel.md#class-jsonstringwriter) when the notification is built.
- **Returns:** nothing.
- **State changes:** in the single-document form, registers the constructor's `text` and `callbacks` as with `open_document`; later calls are ignored.
- **Access:** once, after `start`; tests.
- **Depends on:** [PieceTree.read](../text/piece_tree.hpp.skel.md#function-read)

### function: did_change

- **Inputs:** the document's `DocumentId` (none in the single-document form), and the *pre-edit* `start` and `end` as `LspPosition`s in the negotiated encoding, and the inserted text. The `ChangeEvent` itself is not needed.
- **Returns:** nothing.
- **State changes:** increments that document's version and enqueues an incremental change. If the server only supports full sync, it coalesces changes and sends the full text once 300 ms pass without another change, or at once before a token request, so tokens are never requested for text the server has not seen. Before `didOpen` was sent, only the version changes: the `didOpen` carries the current text.
- **Access:** SemanticHighlighter's `before_change` and `after_change`.

### function: did_change_full

- **Inputs:** the document's `DocumentId` (none in the single-document form).
- **Returns:** nothing.
- **State changes:** increments the version and sends the whole text as one content change.
- **Access:** SemanticHighlighter on `reloaded`.

### function: did_save

- **Inputs:** the document's `DocumentId` (none in the single-document form).
- **Returns:** nothing.
- **State changes:** enqueues `textDocument/didSave`, only if the server's `textDocumentSync.save` asked for it.
- **Access:** SemanticHighlighter on `saved`.

### function: request_tokens

- **Inputs:** the document's `DocumentId` (none in the single-document form); `range`: an optional `LineRange`. With no range, or when the server lacks range support, it requests the full document. The range is sent as `[first, 0]` to `[last, 0]`.
- **Returns:** a request id, or 0 when nothing was sent (the server is not running, or supports neither request). The response arrives through `on_tokens`, carrying the version it was computed for.
- **State changes:** cancels the same document's previous outstanding token request with `$/cancelRequest` and forgets it, so a late answer to it is ignored; other documents' requests are untouched. Returns 0 for a document that is not open.
- **Access:** SemanticHighlighter, debounced.
- **Referred by:** [semantic_highlighter (implementation)](./semantic_highlighter.cpp.skel.md)

### function: fail

- **Inputs:** `reason`: the status text after "LSP off: "; `goodbye`: `Goodbye::orderly` for a server that still answers (it is sent `shutdown` and `exit` and given `kShutdownGraceMs`), `Goodbye::kill` for one that has stopped reading or answering (killed at once, so the editor never waits on it).
- **Returns:** nothing.
- **State changes:** stops the session; the state becomes `failed`, and no restart follows.
- **Access:** private.

### function: tick

- **Inputs:** `now`: a `steady_clock::time_point`.
- **Returns:** `std::optional<time_point>`: the earliest armed deadline (initialize timeout, restart, full-sync flush), or `nullopt`.
- **State changes:** fails a server that has not answered `initialize` by its deadline, respawns a crashed server when its backoff expires, and flushes a coalesced full-text change.
- **Access:** [SemanticHighlighter.tick](./semantic_highlighter.hpp.skel.md#function-tick).

### function: shutdown

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** sends `shutdown` and then `exit` (without waiting for the `shutdown` reply in between; the server handles them in order), waits up to 500 ms for the server to close its output, then [kills](../platform/process.hpp.skel.md#function-kill) it, which unblocks a writer stuck on a full pipe, joins the threads, and reaps the process with `terminate(0)`. Idempotent; the destructor calls it.
- **Access:** on document close or app exit.
- **Depends on:** [ChildProcess.kill](../platform/process.hpp.skel.md#function-kill)

### function: state

- **Inputs:** none.
- **Returns:** the `LspState`. Companion accessors: `encoding()`, `legend()`, `supports_range()`, `supports_full()`, `version()`, and `status_text()`: "LSP: starting <id>", "LSP: <id>", "LSP: restarting <id>" or "LSP off: <reason>".
- **State changes:** none.
- **Access:** SemanticHighlighter.
