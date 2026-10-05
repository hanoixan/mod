---
role: product
stamp: source db201bb9, stand-in d1cafaaf
---
# module: progress

The byte-count progress meter for long synchronous operations on the main thread: copying a sidecar for Save As ([Sidecar.copy_to](../edit/sidecar.hpp.skel.md#function-copy_to)), rewriting it for a prune ([Sidecar.rewrite](../edit/sidecar.hpp.skel.md#function-rewrite)), hashing the anchor's content for a prune ([Document.prune_history](../edit/document.hpp.skel.md#function-prune_history)), and waiting for queued history records to reach the file ([Sidecar.flush](../edit/sidecar.hpp.skel.md#function-flush)). While one of these runs, the event loop is blocked, so the meter cannot wait for the next frame: the operation calls a sink, and the sink draws the status line at once.

The operations only **report**; they never decide how often the meter is drawn. They call the sink once per slice of work (about 1 MiB for the copies and the hash, about 100 ms for `flush`), and the sink throttles drawing. There is no cancel: an operation that reports progress always runs to completion or to its own error.

The sink is injected, never global: [Document](../edit/document.hpp.skel.md#class-document) receives it in `DocumentOptions.progress` and passes it on to its [Sidecar](../edit/sidecar.hpp.skel.md#class-sidecar) in `SidecarSeams.progress`. An empty sink (the default, and what every test that does not check progress passes) means nothing is reported.

Header-only.

- **Owns:** the `Progress` report type, the `ProgressSink` type and the text format of the meter.
- **Access:** public, header-only. Main thread only: sinks are called on the thread that runs the operation, which is always the main thread.
- **Required:** optional — without a sink, long operations show nothing until they finish.
- **Failure modes:** a sink that throws. The sink is App's and must not throw; it catches everything itself, because an exception escaping from inside a sidecar rewrite would abandon a temp file. A sink called very often (a fast disk) costs a function call per slice; drawing is throttled by the sink, not by the caller.
- **Depends on:** none
- **Unknowns:** none

## symbol: Progress

`{ std::string_view label; std::uint64_t done; std::optional<std::uint64_t> total; }`.

- `label` names the operation for the user: `"copying history"`, `"pruning history"`, `"hashing"` or `"writing history"`. It stays valid only for the duration of the call.
- `done` is the number of bytes processed so far. It never decreases during one operation.
- `total` is the expected number of bytes, or `nullopt` when it is not known in advance. It is an estimate: `done` may pass it (for example, record framing adds bytes to a rewrite's payload total), and a reader clamps the percentage at 100.

- **Access:** public.

## symbol: ProgressSink

`std::function<void(const Progress&)>`. Called on the main thread, from inside the operation. May draw to the terminal; must not touch the document or any state the operation is using, and must not throw.

- **Access:** public. Set by [App](../app/app.hpp.skel.md#class-app) in the `DocumentOptions` it passes to `Document.open` and `Document.open_untitled`.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [Document.prune_history](../edit/document.hpp.skel.md#function-prune_history)
- **Referred by:** [Sidecar.copy_to](../edit/sidecar.hpp.skel.md#function-copy_to)
- **Referred by:** [Sidecar.flush](../edit/sidecar.hpp.skel.md#function-flush)
- **Referred by:** [Sidecar.rewrite](../edit/sidecar.hpp.skel.md#function-rewrite)

## function: format_progress

- **Inputs:** a `Progress`.
- **Returns:** the status-line text: `"<label>: <done> of <total> (<percent>%)"` with a known total, or `"<label>: <done>"` without one. Sizes use binary units with one decimal (`"512 B"`, `"1.5 KiB"`, `"12.0 MiB"`, `"3.2 GiB"`, `"1.0 TiB"`); bytes below 1 KiB are shown whole. The percentage is a whole number, `floor(100 × done / total)`: at most 99 while `done < total`, and 100 once `done` reaches or passes `total`; a total of 0 shows 100%.
- **State changes:** none. It is pure.
- **Access:** App's progress sink; tests.
- **Referred by:** [progress_test](../../tests/progress_test.cpp.skel.md)
- **Referred by:** [app](../app/app.hpp.skel.md)
