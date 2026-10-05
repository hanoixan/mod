---
role: test
stamp: source c592f2cb, stand-in e401f057
---
# module: lsp_client_test

Tests for [LspClient](../src/syntax/lsp_client.hpp.skel.md#class-lspclient) and [SemanticHighlighter](../src/syntax/semantic_highlighter.hpp.skel.md#class-semantichighlighter) against a real child process: the scripted [fake_lsp_server](./fake_lsp_server.cpp.skel.md), whose path arrives as the compile definition `MOD_FAKE_LSP_SERVER`. Framing, JSON and `file_uri` are already covered by [json_test](./json_test.cpp.skel.md) and are not repeated here.

**Determinism.** Every client timeout is a deadline run by `tick(now)`, so the tests pass synthetic `now` values and never sleep to make a timer fire. The only real waiting is for the child process. A test helper `pump_until(predicate)` uses the `EventQueue`'s `wake` callback to signal a condition variable, runs `drain`, and re-checks the predicate. It fails the test after a 10-second guard. The guard is only a hang detector: nothing asserts on elapsed time. To catch a response before it is handled, a test waits for the wake without draining.
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)

#### LspClient

- **Initialize handshake.** From the server log:
  - `initialize` carries `processId: null`, `rootUri` and the workspace folder from `file_uri`, the client capabilities exactly as [LspClient.start](../src/syntax/lsp_client.hpp.skel.md#function-start) lists them, and the spec's `initializationOptions` only when not null.
  - It is followed by `initialized`, then `didOpen` with the document text.
  - `did_open` called before the server is initialized is sent after `initialized`, once.
- **Encoding.** `--encoding=utf-8` gives `utf8`; `utf-16` and `absent` give `utf16`.
- **Incremental sync.** `did_change` sends the given range and text, and the version increases by one per call.
- **Full sync** (`--sync=full`, and `--sync=none`):
  - Two `did_change` calls send nothing until `tick(now + 300 ms)`, which sends one full-text change.
  - A `request_tokens` before then flushes the full text first.
  - Before `didOpen` was sent, `did_change` only bumps the version.
- **`did_change_full` and `did_save`.** `did_change_full` sends the whole text. `did_save` is sent only with `--save`.
- **Token requests.**
  - `request_tokens` with a range sends `[first, 0]`–`[last, 0]`. Without range support (`--tokens=full`) it sends a full request. With neither (`--tokens=neither`), or when the client is not running, it returns 0 and sends nothing.
  - A second request sends `$/cancelRequest` for the first, and a late answer to the first is not delivered to `on_tokens`.
  - `--token-error=-32801` delivers nothing, and the next request succeeds.
- **Server requests** (`--server-requests`). The log shows `workspace/configuration` answered with `[null, null]`, and the other two requests answered with `null`. The diagnostics notification produces no reply and no callback.
- **No semantic tokens** (`--tokens=none`). The client becomes `failed` with "LSP off: <id> has no semantic tokens", after `shutdown` and `exit` appear in the log.
- **Initialize failures.**
  - `--init=never`: `tick(start + 10 s)` gives `failed` and "LSP off: <command> did not answer initialize". `tick(start + 9.999 s)` does not.
  - `--init=error`: gives `failed`.
- **Crash and restart.**
  - `--crash=once:<marker>`: after the crash, `state()` is `stopped` with "LSP: restarting <id>". `tick` before the 1 s backoff does not respawn; at 1 s it does.
  - The new session sends `initialize`, then `didOpen` with the current text, including edits made while it was down, and keeps the document version. `on_ready` runs again.
- **Repeated crashes** (`--crash=always`). Restarts happen at the 1 s, 4 s and 16 s backoffs. After that, `failed` with "LSP off: <command> keeps exiting".
- **Server not found.** A command naming a missing executable makes `start` return `not_found`, sets `failed` with the spawn error, and never restarts.
- **Shutdown.**
  - `shutdown` sends `shutdown` then `exit` (logged in that order, with no `params`), reaps the child, and is idempotent. The destructor after an explicit `shutdown` does nothing more.
  - `--hang-on-shutdown`: the child is killed after the grace period and `shutdown` still returns. This is the one test that spends the 500 ms.
- **Invalid UTF-8.** A document holding invalid bytes reaches the server with one U+FFFD per invalid byte (checked in the log's `didOpen` text).

#### SemanticHighlighter

Documents are real files in the scratch directory, opened with `Document.open`. The highlighter is registered as a listener, as App does, and its spec names the fake server.

- **Decoding.**
  - A full response becomes byte spans with the expected `lsp_type`, `lsp_variable` and `kModDeprecated` values from `spans_for_line`.
  - A line holding `é` and `😀` before a token gives the same byte spans under `--encoding=utf-8` and `utf-16`.
  - `--legend-unknown` words get `Style::Default` and are not cached.
- **Range requests.**
  - Scrolling with `visible_range_changed` requests the visible lines plus 100 either way once `tick` passes the 150 ms debounce.
  - A range response replaces only that range's spans.
  - `--stray-token` tokens are dropped.
  - With `--tokens=range`, the first request covers lines 0–200 before any scroll.
- **Edits.**
  - Between an edit and fresh tokens, spans after the edit shift by the byte delta, a span the edit lands inside covers the typed text, and spans inside removed text disappear.
  - After a sequence of inserts and deletes across lines, under incremental and full sync, the fresh tokens match the document. This proves the fake's copy of the text equals it, so the `didChange` positions were right.
- **Stale tokens.** A response that arrives after a further edit (the wake is seen, then the edit is made, then the queue is drained) is dropped, and is re-requested after the debounce.
- **Reload and save.** `reloaded` clears the cache and sends one full-text change. `saved` sends `didSave` with `--save`.
- **Size cap.** A document over the spec's `max_file_bytes` gets no client, and `status()` reads "LSP off: file too large". Several documents on one server: each opened once with its own text under one `initialize`, versions, changes and token requests kept apart (one document's request never cancels another's), `didClose` on closing with the server running on for the other and back to `initialized` after the last, and a restart opening every document again with its current text and version. Project roots: the nearest folder with a marker, the nearest of several, the file's own folder without one (in a folder outside the build tree, which has its own `compile_commands.json`). The pool shares a server per language and root, keeps it while anyone holds it, shuts it down with the last release, and starts a fresh one after; two highlighted documents in one project share one server, which gets two `didClose` and `exit` when both go. Legend mapping: a server type that is not standard maps to the standard type its name ends with (`selfParameter` → parameter), the longest winning, and to nothing when none matches; `defaultLibrary` and `declaration` (and `definition`) reach the spans' modifier bits. Misbehaving servers: requests answered as the protocol expects (accepted, applyEdit declined, unknown methods refused); a server that stops reading is given up on once 64 MiB are queued; garbage, a frame claiming 999 999 999 999 bytes and a flood of 20 000 notifications leave the client working. 100 000 tokens on one long line decode in linear time; a burst of 20 000 edits is quick and the server ends up with the right text, incremental and full sync. A server that runs five minutes between crashes is restarted every time from the 1 s backoff; giving up on a server that hangs on shutdown does not wait for it. The reloaded file is replaced by a rename, as most editors save, since Windows refuses to shorten a file in place while mod has it mapped.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:**
  - Flaky timing-based tests are forbidden. Client timers are driven through `tick(now)`, and process I/O is awaited through the queue's wake, never with sleeps.
  - A stuck child must not hang CTest: every wait has the 10-second guard, and each fixture's teardown calls `shutdown`, which kills the child.
  - The test needs `fake_lsp_server` to be built first, which `tests/CMakeLists.txt` declares as a dependency.
- **Depends on:** [LspClient](../src/syntax/lsp_client.hpp.skel.md#class-lspclient)
- **Depends on:** [SemanticHighlighter](../src/syntax/semantic_highlighter.hpp.skel.md#class-semantichighlighter)
- **Depends on:** [Document](../src/edit/document.hpp.skel.md#class-document)
- **Depends on:** [fake_lsp_server](./fake_lsp_server.cpp.skel.md)
- **Depends on:** [LspServerPool](../src/syntax/lsp_pool.hpp.skel.md#class-lspserverpool)
- **Depends on:** [find_project_root](../src/syntax/language_config.hpp.skel.md#function-find_project_root)
- **Depends on:** [time_budget](./time_budget.hpp.skel.md#function-time_budget)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
