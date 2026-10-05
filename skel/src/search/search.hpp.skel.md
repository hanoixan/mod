---
role: product
stamp: source 19e39572, stand-in da8e577f
---
# module: search

Find and replace over the whole document. Searching streams over the piece tree in windows. It runs cooperatively on the main thread in time slices, so the UI stays responsive on multi-GB files and Esc cancels it. Replace-all is a single undo node.

- **Owns:** the current query, the compiled `Regex`, the search progress cursor, and the last match.
- **Access:** public. One per App, driven by the find/replace [Prompt](../ui/prompt.hpp.skel.md#class-prompt).
- **Required:** always.
- **Failure modes:** a replace-all on a huge file with millions of matches makes one undo node with millions of ops. The node is persisted with its payloads streamed and the ops encoded compactly. Memory for the op list is about 40 B per match, which is accepted. A search that wraps around finds the same match again, so wrapping is reported once ("wrapped") and the search stops after one full lap.
- **Depends on:** [Regex](./regex.hpp.skel.md#class-regex)
- **Depends on:** [Document.apply](../edit/document.hpp.skel.md#function-apply)
- **Depends on:** [PieceTree.read](../text/piece_tree.hpp.skel.md#function-read)
- **Unknowns:** none

## symbol: SearchOptions

`{ bool regex; bool case_insensitive; bool whole_word; bool wrap; }`. Toggled in the find bar with Alt+R, Alt+C and Alt+W while it is focused. Defaults: regex on, case-sensitive, not whole-word, wrap on.

- **Access:** public.

## class: Searcher

- **Inputs:** `doc`: a `Document&`; `editor`: an `Editor&`.
- **State changes:** `idle → searching(direction, progress) → found | not_found | canceled`. Invariant: the last match is invalidated by any document change; the version is compared.
- **Owns:** see the module.
- **Access:** App and Prompt.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [search (implementation)](./search.cpp.skel.md)
- **Referred by:** [prompt](../ui/prompt.hpp.skel.md)
- **Referred by:** [search_test](../../tests/search_test.cpp.skel.md)
- **Referred by:** [document_slot](../app/document_slot.hpp.skel.md)

### function: set_query

- **Inputs:** `find_text`, `options`.
- **Returns:** `Status`: a compile error for display.
- **State changes:** recompiles and resets progress.
- **Access:** Prompt, on each keystroke (incremental search highlights the next match).

### function: find_next

- **Inputs:** `from`: an offset (the cursor); `direction`: `Direction::forward` or `Direction::backward`.
- **Returns:** nothing. The result arrives asynchronously through `step`.
- **State changes:** starts a search job. Forward finds the first match starting at or after `from`; backward finds the last match starting before `from`. When the last match is still valid and is the editor's selection, forward starts at its end and backward at its start, so repeated F3 and Shift+F3 step through matches, and an empty match is stepped over by one code point. Matches follow the usual global-matching rule: after a non-empty match, an empty match at its end is a match of its own (as in Perl and PCRE2's own global replace). With `wrap`, a search that reaches the end (or start) continues from the other end up to `from`. A document change during a job cancels it (`canceled`).
- **Access:** Enter and Shift+Enter in the find bar, and F3 / Shift+F3 (`FindNext` / `FindPrev`) from anywhere, using the last query set with `set_query`.

When no query has been set in this session, `find_next` is not called: F3 and Shift+F3 open the find bar instead, as Ctrl+F does; see [App.run_command](../app/app.hpp.skel.md#function-run_command).

### function: step

- **Inputs:** `budget_bytes`: about 8 MiB per event-loop iteration (`kStepBudget`).
- **Returns:** a `StepResult { kind; match; count; wrapped; line_too_long; message; }` whose `kind` is `idle` (no job), `in_progress`, `found` (with `match`), `not_found`, `canceled`, or `replaced` (a replace-all job finished, with the count in `count`). An engine error ("pattern too complex") ends the job with `not_found`, or `replaced` with the count so far for a replace-all, and the reason in `message`. `wrapped` is set on the result that first crosses the end of the document; `line_too_long` when a line longer than the 16 MiB window cap was searched only partly ("line too long, match may be missed").
- **State changes:** advances progress. On `found`, calls [Editor.select_range](../edit/editor.hpp.skel.md#function-select_range). Backward search scans windows in reverse order and keeps the last match in each window.
- **Access:** App.run, while a job is active. The loop uses a zero wait timeout during the job.

### function: last_match

- **Inputs:** none.
- **Returns:** the last found `Match`, or `nullopt` when there is none or the document changed since (the version is compared). `active()` tells whether a job is running.
- **State changes:** none.
- **Access:** EditorView (match highlight) and App.

### function: cancel

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** goes to `idle`.
- **Access:** Esc.

### function: replace_current

- **Inputs:** `replacement`: the template; it is expanded only in regex mode (see [Regex.expand_replacement](./regex.hpp.skel.md#function-expand_replacement)).
- **Returns:** `Status`: a template error, or `canceled` when the selection is not the last match (nothing replaced).
- **State changes:** if the current selection equals the last match, replaces it with one `replace` node, then searches for the next match.
- **Access:** the Replace button or key in the Prompt.

### function: replace_all

- **Inputs:** `replacement`.
- **Returns:** `Status`: a template error found before anything is replaced (the template is checked against the pattern's groups first). The count arrives through `step`, as `replaced`.
- **State changes:** starts a replace-all job that `step` advances; inside `Document.begin_group(replace_all)`, walks the matches front to back and applies each one, adjusting for the length delta. Time-sliced like `step`, with a progress display. Canceling midway keeps the replacements already made, closes the group, and leaves them undoable as one node. Replacements are made front to back from the start of the document; each search continues after the replacement text, and an empty match advances by one code point, so a replacement is never matched again. While the job runs the group stays open, so App must not dispatch other edits until it ends (Esc cancels it).
- **Access:** Prompt.
