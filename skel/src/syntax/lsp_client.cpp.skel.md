---
role: product
unit: ./lsp_client.hpp.skel.md
stamp: source 9aa86a46, stand-in 0c042773
---
# module: lsp_client (implementation)

Implements [LspClient](./lsp_client.hpp.skel.md#class-lspclient).

- Each server process is one *session*: the child, the outgoing queue, a stop flag, a reader-done flag, and the two threads, behind one mutex and condition variable. Sessions are numbered; a message or exit posted by an older session is ignored.
- Reader thread: reads 64 KiB at a time into a [FrameParser](./lsp_client.hpp.skel.md#class-frameparser), so messages are parsed with `Json` on this thread, and posts each message. At EOF or a read error it sets reader-done, notifies, and posts an exit event.
- Writer thread: waits on the condition variable over a `std::deque<std::string>` of framed messages, and exits once stopped and drained. A write error drops the queue and stops it; the reader then sees the exit.
- Main-thread dispatch: matches responses to pending ids, answers server requests, and drops notifications. Posted closures capture a `std::shared_ptr<LspClient*>` that the destructor clears, as Document does.
- Messages are built as `Json` and serialized with `dump`, except `didOpen` and full-text changes, whose text is streamed into the body with a [JsonStringWriter](./json.hpp.skel.md#class-jsonstringwriter). JSON-RPC `params` is omitted, not `null`, for `shutdown` and `exit`.
- An unexpected exit stops the session (kill, join, reap) and arms the restart timer; the restart count is per `start`.

- **Owns:** the threads and the frame parser.
- **Access:** internal.
- **Required:** optional — as for the header.
- **Failure modes:** a `Content-Length` that does not match the body. Resynchronize by searching for the next `Content-Length:` header, and log it; see [FrameParser](./lsp_client.hpp.skel.md#class-frameparser). SIGPIPE from writing to a dead server is ignored process-wide by `main`, so the write fails with EPIPE instead.
- **Depends on:** [LspClient](./lsp_client.hpp.skel.md#class-lspclient)
- **Depends on:** [FrameParser](./lsp_client.hpp.skel.md#class-frameparser)
- **Depends on:** [JsonStringWriter](./json.hpp.skel.md#class-jsonstringwriter)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
