---
role: product
untested: exercised only indirectly, when lsp_client_test starts the fake language server through LspClient
stamp: source c16a8ba7, stand-in 8f0d0662
---
# module: process

Spawns a child process with piped stdin and stdout. Used only to run language servers.

- **Owns:** the `ChildProcess` interface.
- **Access:** public. Owned by [LspClient](../syntax/lsp_client.hpp.skel.md#class-lspclient).
- **Required:** optional — without it there is no LSP coloring.
- **Failure modes:** the executable is not found: `not_found`, and LSP is disabled for that language with a status message. The child dies, so reads return EOF and writes fail with EPIPE. SIGPIPE must be ignored process-wide; `main` sets `SIG_IGN`. The child starts with SIGPIPE at its default and no signal blocked (`POSIX_SPAWN_SETSIGDEF`, `POSIX_SPAWN_SETSIGMASK`), so a server still dies of a closed pipe; a failing spawn setup call fails the spawn. If the child's stderr is not drained, the child blocks, so stderr goes to the null device, or to the log file when `MOD_LOG` is set.
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)
- **Unknowns:** none

## class: ChildProcess

- **Inputs:** `argv`: a non-empty vector of strings, resolved through `PATH`; `cwd`: the working directory, which is the edited file's directory.
- **State changes:** `running → exited`. Pipes are closed on destruction, and the child is terminated if it is still running.
- **Owns:** the child pid or handle, its stdin write end and its stdout read end.
- **Access:** `write` comes from the LSP writer thread and `read` from the LSP reader thread. `kill` runs on the main thread while those threads may still be blocked, to unblock them; `terminate` and the destructor run on the main thread after both threads are joined.
- **Referred by:** [process_posix](./process_posix.cpp.skel.md)
- **Referred by:** [LspClient.start](../syntax/lsp_client.hpp.skel.md#function-start)
- **Referred by:** [lsp_client](../syntax/lsp_client.hpp.skel.md)
- **Referred by:** [process_test](../../tests/process_test.cpp.skel.md)

### function: spawn

- **Inputs:** `argv`, `cwd`.
- **Returns:** `Result<std::unique_ptr<ChildProcess>>`.
- **State changes:** creates the process and its pipes. The parent's pipe ends get `CLOEXEC`.
- **Access:** [LspClient.start](../syntax/lsp_client.hpp.skel.md#function-start).
- **Referred by:** [LspClient.start](../syntax/lsp_client.hpp.skel.md#function-start)
- **Referred by:** [lsp_client](../syntax/lsp_client.hpp.skel.md)

### function: write

- **Inputs:** `bytes`.
- **Returns:** `Status`. Blocks until every byte is written.
- **State changes:** writes to the child's stdin.
- **Access:** a single writer thread.

### function: read

- **Inputs:** `buf`.
- **Returns:** `Result<size_t>`: 0 means EOF. Blocks until data arrives.
- **State changes:** consumes the child's stdout.
- **Access:** a single reader thread.

### function: kill

Needed because the reader thread blocks in `read` until the child's stdout closes, and the writer can block in `write` on a full pipe when the server stops reading, so neither can be joined before the child is gone; `terminate` must not run first, because it closes the stdin fd the writer may be using.

- **Inputs:** none.
- **Returns:** nothing; `noexcept`.
- **State changes:** sends SIGKILL unless the child was already reaped (a no-op for a child that has exited but is not yet reaped). It touches no fd and does not reap: a blocked `read` then returns EOF and a blocked `write` fails with EPIPE, and `terminate` reaps afterwards.
- **Access:** main thread, during [LspClient.shutdown](../syntax/lsp_client.hpp.skel.md#function-shutdown), before the threads are joined.
- **Referred by:** [LspClient.shutdown](../syntax/lsp_client.hpp.skel.md#function-shutdown)

### function: terminate

- **Inputs:** `grace_ms`.
- **Returns:** `std::optional<int>`: the exit status if it is known, or 128 + the signal number when the child was killed by a signal.
- **State changes:** closes stdin, waits up to `grace_ms`, then sends SIGTERM and waits up to `max(grace_ms, 100)` ms more, then SIGKILL, and reaps it. A second call returns the status found by the first.
- **Access:** main thread, during LspClient shutdown.
