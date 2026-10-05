---
role: product
stamp: source d256af25, stand-in fa941ec6
---
# module: event_queue

The single crossing point between worker threads and the main thread. Workers post closures, and the main loop drains them and runs them on the main thread, so all editor state stays single-threaded.

- **Owns:** a mutex-protected FIFO of `std::move_only_function<void()>` tasks.
- **Access:** public, header-only. One instance, owned by [App](../app/app.hpp.skel.md#class-app) and passed by reference to workers when they are constructed.
- **Required:** always.
- **Failure modes:** a task posted after `App` begins shutdown is dropped. `close()` makes `post` a no-op, so workers must tolerate their posts being silently discarded. A worker that posts faster than the main loop drains, such as a flood of LSP messages, grows memory. Accept this; the LSP client coalesces responses.
- **Unknowns:** none

## class: EventQueue

- **Inputs:** `wake`: a `std::move_only_function<void()>` called after every successful `post`. It is wired to [Terminal.wake](../platform/terminal.hpp.skel.md#function-wake).
- **State changes:** invariant: tasks run in the order they were posted, on the main thread only, and each runs exactly once unless the queue was closed first.
- **Owns:** pending tasks.
- **Access:** `post` and `close` are thread-safe. `drain` is main-thread only.
- **Depends on:** [Terminal.wake](../platform/terminal.hpp.skel.md#function-wake)
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [event_queue_test](../../tests/event_queue_test.cpp.skel.md)

### function: post

- **Inputs:** `task`: a closure that captures its data by value. Any references it holds must outlive the queue.
- **Returns:** `true` if enqueued, or `false` if the queue is closed.
- **State changes:** appends to the FIFO, then calls `wake` outside the lock.
- **Access:** any thread.
- **Referred by:** [sidecar](../edit/sidecar.hpp.skel.md)
- **Referred by:** [lsp_client](../syntax/lsp_client.hpp.skel.md)
- **Referred by:** [line_scanner](../text/line_scanner.hpp.skel.md)

### function: drain

- **Inputs:** none.
- **Returns:** the number of tasks run.
- **State changes:** swaps the FIFO out under the lock, then runs each task with the lock released. Tasks may call `post` re-entrantly; those tasks run on the next drain. When a task throws, the tasks after it in the batch go back to the front of the FIFO (unless the queue is closed) before the exception propagates, so each still runs exactly once, in order.
- **Access:** main thread, once per loop iteration.
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)

### function: close

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** drops pending tasks. Every later `post` returns `false`.
- **Access:** called by `App` during shutdown, before worker threads are joined.
