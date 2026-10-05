---
role: product
untested: no test has been written for it yet
stamp: source 04bd8956, stand-in fa34f30a
---
# module: prompt

The bottom-of-screen input bar and its modal overlays. It is a single reusable line editor (left, right, home, end, backspace, history-free) plus mode-specific behavior:

- `find`: one field. Searches incrementally as you type. Enter finds next, Shift+Enter finds previous. Alt+R, Alt+C and Alt+W toggle the search options. Tab switches to `replace`.
- `replace`: find and replace fields. Enter replaces the current match and moves to the next. Alt+A replaces all.
- Open and Save As are not prompts: they open the [file dialog](./file_dialog.hpp.skel.md#class-filedialog).
- `goto_line`: a decimal number, resolved with a forced [PieceTree.line_start](../text/piece_tree.hpp.skel.md#function-line_start). Opened by Ctrl+G or Edit > Go to Line….
- `setting`: a decimal number, prefilled with the current value of an integer setting, with the label "<setting label> (<min> to <max>):". Opened by Enter on an integer row of the User Settings panel and by an integer item among the Options menu's recent settings. It accepts a whole number within the setting's range; anything else is rejected with the inline error "enter a whole number from <min> to <max>" and the prompt stays open.
- `prune_age`: a decimal number of days, for the Undo History pane's Trim History… and the slow-load offer. The question shows the age of the oldest change, for example "Prune history older than how many days? Oldest change: 412 days ago." It accepts a whole number from 0 up to and including 100 000. Anything else is rejected with the inline error "enter a whole number of days" and the prompt stays open. Esc cancels, and nothing is pruned.
- Questions with choices are not a prompt kind: they are the [ConfirmBar](./confirm_bar.hpp.skel.md#class-confirmbar).
- `info`: a scrollable read-only overlay for Help > About.

- **Owns:** the field buffers, the field cursors, and the mode.
- **Access:** public. One instance, owned by App. Main thread.
- **Required:** always.
- **Failure modes:** a pasted path containing a newline is truncated at the first newline. Esc always closes the prompt and cancels any running search.
- **Depends on:** [Searcher](../search/search.hpp.skel.md#class-searcher)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none

## symbol: PromptKind

`enum class PromptKind { find, replace, goto_line, setting, color, prune_age, info }`. `color` is a one-line field for a color spec, validated by its callback.

- **Access:** public.

## class: Prompt

- **Inputs:** a `Searcher*`, possibly null, replaced with `set_searcher`. A pointer rather than a reference because App rebuilds the Searcher with the Document on File > Open, while the Prompt (whose `open` submit callback runs that rebuild) lives on.
- **State changes:** `closed | open(kind, fields, on_submit)`. Re-entrancy: a submit or choice callback may open the prompt again (Save As, then a confirm). The callback is moved out and the prompt marked closed *before* it runs, so whatever the callback opens stays open, and the Prompt object itself is never destroyed while in use.
- **Owns:** see the module.
- **Access:** App routes keys here while it is open. The prompt has focus over the editor but not over the menu.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [prompt (implementation)](./prompt.cpp.skel.md)

### function: open

- **Inputs:** `kind` (one of `goto_line`, `setting`, `color`, `prune_age`); `label`: the question shown before the field; `initial_text`; `on_submit`: a callback receiving the field text and returning an optional error message. A returned message is shown inline and the prompt stays open (the `setting` and `prune_age` range checks are made by the callback, so the prompt needs no knowledge of App); `nullopt` closes it.
- **Returns:** nothing.
- **State changes:** opens the prompt.
- **Access:** App command handlers.

### function: open_find

- **Inputs:** `allow_replace`: false in a read-only view, where Tab does not switch to replace (default true). `initial_text`: the selected text, or the last query; `origin`: the cursor offset the incremental search starts from; `cursor`: a callback returning the editor's cursor, which Enter and Shift+Enter search from (Searcher then steps past a match that is the selection).
- **Returns:** nothing.
- **State changes:** opens the `find` prompt. Each edit of the find field calls `Searcher.set_query` and `Searcher.find_next(origin)`; App advances the job with `Searcher.step`. Enter and Shift+Enter call `Searcher.find_next` from `cursor()`, forward or backward. In `replace`, Enter calls `Searcher.replace_current` (which moves to the next match), or finds the first match when the selection is not one; Alt+A calls `Searcher.replace_all`. The search options persist between openings. Shift+Enter is delivered only by terminals that report modified Enter (`CSI 13;2u` or `CSI 27;2;13~`); elsewhere Edit > Find Previous and Shift+F3 remain. Tab opens the replace field; in `replace`, Tab moves between the two fields.
- **Access:** App, on `Find`.

### function: open_info

- **Inputs:** `title`; `text`: lines separated by LF.
- **Returns:** nothing.
- **State changes:** opens the `info` overlay. Up/Down/PageUp/PageDown scroll; Esc, Enter and `q` close it.
- **Access:** App, for Help > About.

### function: close

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** goes to `closed` and drops the callbacks; closing `find` or `replace` cancels the running search. `is_open()` and `kind()` report the state; `field(i)` and `error()` expose the fields and inline error. `last_query()` is the find bar's query: the open find or replace prompt's, else the one it had when another kind of prompt (Go to Line, Save As, a setting's value) was opened over it, which App prefills the next find with.
- **Access:** App.

### function: set_searcher

- **Inputs:** a `Searcher*`, possibly null.
- **Returns:** nothing.
- **State changes:** replaces the searcher; closes an open `find` or `replace` prompt, whose match belonged to the old document.
- **Access:** App, after rebuilding the Searcher.

### function: handle_event

- **Inputs:** an `InputEvent`.
- **Returns:** `consumed` or `closed`. Pasted text goes into the field, truncated at the first newline.
- **State changes:** edits fields and invokes Searcher or `on_submit`.
- **Access:** App.

### function: render

- **Inputs:** a `Screen&`; `area`: the rows App gives it (one row, two for `replace`, at the screen's bottom below the status lines; most of the screen for `info`, drawn over what is above the bottom band). `rows_wanted(screen_rows, screen_cols)` tells App how many rows the open prompt needs. The `info` overlay is drawn over the text area without shrinking it, so opening and closing it never scrolls the text.
- **Returns:** nothing.
- **State changes:** draws the prompt and places the cursor.
- **Access:** App.render.
