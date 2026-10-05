---
role: test
stamp: source 6f64dfab, stand-in 29b6a0a3
---
# module: fake_lsp_server

A scripted language server for [lsp_client_test](./lsp_client_test.cpp.skel.md). It is a small standalone executable that speaks just enough LSP over stdio to drive [LspClient](../src/syntax/lsp_client.hpp.skel.md#class-lspclient) and [SemanticHighlighter](../src/syntax/semantic_highlighter.hpp.skel.md#class-semantichighlighter) through every case in their failure-mode lists, with no real language server installed. It replaces the throwaway harness the syntax batch used and deleted.

It is a test helper, not a test. It is built by [tests/CMakeLists.txt](./CMakeLists.txt.skel.md) outside the `MOD_TESTS` loop, is never registered with CTest, defines its own `main`, and links `mod_core`, so that it reuses [FrameParser](../src/syntax/lsp_client.hpp.skel.md#class-frameparser) to split frames and [Json](../src/syntax/json.hpp.skel.md#class-json) to parse and build messages. It reads stdin and writes stdout with plain blocking I/O on one thread. It never touches the terminal. It keeps one text per open document, chosen by the `textDocument.uri` of each message, and forgets a document on `didClose`, so several documents can share it. Misbehaving modes: --stop-reading (never reads after initialize), --flood=N (N notifications after initialized), --garbage (noise, an impossible frame and a broken header before each token response); --server-requests also sends workspace/applyEdit and an unknown method.

- **Owns:** its copy of the document text, its scenario flags, and its log file.
- **Access:** spawned only by `lsp_client_test`, through the client under test. The test passes its path as the first element of `LanguageServerSpec.command`, followed by the flags below.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** a frame it cannot parse makes it exit with status 2, so a framing bug in the client shows up as a crash in the test rather than a hang. It exits with status 0 at stdin EOF. It must exit promptly after `exit`, because the client waits up to 500 ms of wall-clock time for that before killing it, and the tests must not spend that wait except in the one case that tests the kill.
- **Depends on:** [FrameParser](../src/syntax/lsp_client.hpp.skel.md#class-frameparser)
- **Depends on:** [Json](../src/syntax/json.hpp.skel.md#class-json)
- **Depends on:** [LSP 3.17 specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
- **Referred by:** [lsp_client_test](./lsp_client_test.cpp.skel.md)

## function: main

- **Inputs:** `argv` flags, all optional. Each one selects a single behavior, and the defaults describe a well-behaved server.

  Capabilities announced in the `initialize` result:
  - `--encoding=utf-8|utf-16|absent`: the `positionEncoding` capability. The default is `utf-16`.
  - `--sync=incremental|full|none`: `textDocumentSync.change` is 2, 1 or 0. The default is `incremental`.
  - `--save`: announces `textDocumentSync.save`, so the client sends `didSave`.
  - `--tokens=both|full|range|neither|none`: which `semanticTokensProvider` requests are supported. `neither` announces a provider with `range` and `full` both false; `none` omits the provider. The default is `both`.
  - `--legend-unknown`: adds a token type `fakeLabel`, whose name ends with no standard type,, and uses it for words that start with `Z`.
  - `--legend-extra=NAME[,NAME…]`: appends these type names to the legend (after `fakeLabel` when both are given), and uses the first for words that start with `S`.

  Initialization behavior:
  - `--init=answer|never|error`: answer `initialize`, never answer it, or answer with a JSON-RPC error. The default is `answer`.
  - `--crash=always|once:<marker path>`: exit with status 1, without a reply, on the first token request.
    - `always` crashes in every run.
    - `once` crashes only when the marker file does not exist, and creates it first, so the restarted server behaves normally.

  Behavior during the session:
  - `--server-requests`: right after `initialized`, sends `workspace/configuration` with two items, `window/workDoneProgress/create` and `client/registerCapability`, plus a `textDocument/publishDiagnostics` notification.
  - `--token-error=<code>`: answers the first token request with that JSON-RPC error code (for example `-32801`, `ContentModified`).
  - `--stray-token`: adds to every range response one token on a line outside the requested range.
  - `--hang-on-shutdown`: answers `shutdown`, then ignores `exit` and keeps reading. The client then has to kill it.

  Logging:
  - `--log=<path>`: appends every received message to the file as one compact JSON line, and flushes after each line, before acting on the message. So once the test has a response, every message sent before that request is in the log. Notifications that no request follows (`didSave`, `shutdown`, `exit`) are readable once the test has awaited the child's exit, or by polling the file under the 10-second guard.
- **Returns:** the exit status: 0 after `exit` or at EOF, 1 for a scripted crash, and 2 for an unparseable frame.
- **State changes:**
  - **Document text.** Keeps its own copy of the text: set by `didOpen`, then updated by each `didChange` according to the announced sync kind, with ranges in the announced encoding.
  - **Server requests.** Records the client's answers to its own requests in the log.
- **Access:** process entry.

#### Tokens

The fake derives tokens from its own copy of the text, so a wrong `didChange` from the client shows up as wrong coloring in the test:

- Every maximal run of `[A-Za-z_][A-Za-z0-9_]*` is one token. Its type is `type` when it starts with an uppercase letter, otherwise `variable`; with `--legend-unknown`, a run starting with `Z` gets `fakeLabel` instead.
- A run that starts with `old_` also carries the `deprecated` modifier.
- Positions use the announced encoding and the relative 5-integer format.
- A range request returns only tokens on lines `[start.line, end.line)`, plus the stray token when `--stray-token` is set.
- The legend is fixed: types `[namespace, type, class, function, variable]`, then `fakeLabel` when enabled, then the `--legend-extra` names; modifiers `[declaration, deprecated, readonly, documentation]`.
