---
role: product
stamp: source 4ff19f0c, stand-in 182ed99d
---
# module: confirm_bar

The **confirm bar**, as in ../modi/: a question with buttons, drawn in App's band at the bottom of the screen, below the status lines (above any prompt). From the top: a rule, the question (wrapped to the width, one or more lines), then the buttons on one row, `[>Save<]  [Discard]  [Cancel]`, the focused one in `listSelected` with `>`/`<` marks and every button's underlined first letter as its key.

Keys: Tab, Right and Shift+Tab, Left move the focus (wrapping); Enter chooses the focused button; a button's first letter chooses it (any case, without Ctrl or Alt); Esc chooses the caller's Esc choice. Every other key is consumed. The bar closes before its callback runs, so a callback may open another question.

The questions: Save / Discard / Cancel ("Save changes before closing?"), Reload / Keep (the file was replaced on disk), Reload alone (the file was modified in place on disk; Esc also means Reload), Write in place / Cancel (atomic save impossible), Save / Cancel (Clear History… on a document with unsaved changes), Clear / Cancel (Clear History…), Prune… / Not now (the slow-load offer), and Prune / Cancel (confirming a prune with its counts).

- **Owns:** the question, the choices, the focus and the callback.
- **Access:** public. One instance, owned by App, which gives it every key while it is open (before the menu). Main thread.
- **Required:** always.
- **Failure modes:** a question too long for the screen is cut at the bottom of the rows App gives it.
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none

## class: ConfirmBar

- **Inputs:** none at construction.
- **State changes:** `closed ⇄ open`.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [confirm_bar_test](../../tests/confirm_bar_test.cpp.skel.md)
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [confirm_bar (implementation)](./confirm_bar.cpp.skel.md)

### function: open

- **Inputs:** `question`; `choices`: one to three labels; `esc_choice`: the index Esc chooses; `on_choice`: `void(int)`, called with the chosen index.
- **Returns:** nothing.
- **State changes:** opens with the first button focused; opening while open replaces the question. `is_open()` says whether it is open.
- **Access:** App.

### function: handle_key

- **Inputs:** a `KeyEvent`.
- **Returns:** nothing.
- **State changes:** as the keys above.
- **Access:** App.

### function: rows

- **Inputs:** `cols`: the width.
- **Returns:** the rows it needs: the rule, the question's lines, the buttons; 0 when closed.
- **State changes:** none.
- **Access:** App's layout.

### function: render

- **Inputs:** a `Screen&`; the area App gives it.
- **Returns:** nothing.
- **State changes:** draws it as above and hides the cursor.
- **Access:** App.render.
