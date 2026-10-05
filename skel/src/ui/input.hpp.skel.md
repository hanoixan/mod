---
role: product
stamp: source 833be9e2, stand-in 07b952c5
---
# module: input

Decodes the raw terminal byte stream into key, paste and text events. It owns all knowledge of escape sequences: xterm CSI and SS3 with modifier parameters, ESC-prefix Alt, control characters, and bracketed paste.

- **Owns:** the partial-sequence buffer and the paste accumulator.
- **Access:** public. One instance, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** always.
- **Failure modes:**
  - Alt+key and a lone Esc look alike. A lone ESC is reported only after `esc_timeout` (25 ms) with no follow-up byte; while one is pending, `App` waits with that timeout. Over slow SSH links, Alt+key can split and be misread as Esc followed by a key. This is accepted.
  - Ctrl+Home/End, Shift+arrows and Ctrl+arrows depend on the terminal emitting xterm modifier sequences (`CSI 1;5H`, `CSI 1;2C`). macOS Terminal.app sends nothing for some of these by default. Document the terminal settings needed; the menu offers the same commands.
  - Ctrl+Backspace usually arrives as 0x08, the same as Ctrl+H. The decoder cannot see the terminal's erase setting, so 0x08 is always reported as Backspace with Ctrl (word-delete through the Keymap) and 0x7F as Backspace. A terminal whose plain Backspace sends 0x08 therefore deletes a word per press; the xterm-compatible terminals targeted here (and Windows Terminal) send 0x7F.
  - An unknown sequence is consumed to its final byte and dropped, never inserted as text.
  - A long bracketed paste is handed on in pieces of at most `kPasteChunk` (1 MiB), each cut before a UTF-8 continuation byte and never between CR and LF (a piece goes out only once the byte after its cut has arrived), so memory stays bounded. Every piece but the last has `more` set; a paste handed on in pieces always gets a last one, empty if nothing is left, so the receiver knows where it ends. A paste whose `CSI 201~` never arrives ends after `kPasteIdle` (1.5 s) with no input: what arrived is the paste, and later keys are keys again, never swallowed.
  - A terminal's replies, OSC (`ESC ]` and a number) and DCS (`ESC P` and a digit, `$`, `+` or `!`) strings ending in BEL or `ESC \`, are dropped whole, never typed; one still open after `kPasteIdle` is dropped as far as it went; one longer than `kMaxStringBytes` (64 KiB) is dropped as it arrives, through its BEL or `ESC \` however the reads split it (an `ESC` at the end of one read and its `\` in the next included), or until `kPasteIdle` passes with no input, so no part of it is ever typed. `ESC ]` or `ESC P` followed by anything else is Alt+`]` or Alt+Shift+`P`.
  - A `CSI … u` key whose code point is a control (C0 other than Tab, Enter, Esc and Backspace, DEL, or C1) is dropped, never typed.
- **Depends on:** none
- **Unknowns:** none

## symbol: Key

`enum class Key`. The values are:

- `Char`, for printable text.
- `Enter`, `Tab`, `BackTab`, `Backspace`, `Delete`, `Insert`, `Escape`.
- `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `PageUp`, `PageDown`.
- `F1`…`F12`.
- `Ctrl` combined with a letter, as `CtrlLetter`.

- **Access:** public.
- **Referred by:** [list_cursor](./list_cursor.hpp.skel.md)

## symbol: KeyEvent

`{ Key key; char32_t ch; uint8_t mods; }`, comparable with `==`.

- `mods` is a bit set of Shift, Alt and Ctrl: the public constants `kShift` (1), `kAlt` (2) and `kCtrl` (4), as in xterm's modifier parameter minus one.
- `ch` is set for `Char` (U+FFFD for an invalid byte) and `CtrlLetter` (the lowercase letter, or one of `@\]^_` and space; `mods` includes `kCtrl`). 0x08 arrives as `Backspace` with `kCtrl`.
- Text is reported one code point per event, and coalescing happens in the Document.

- **Access:** public.
- **Referred by:** [keymap](../app/keymap.hpp.skel.md)
- **Referred by:** [history_view](./history_view.hpp.skel.md)
- **Referred by:** [keymap_view](./keymap_view.hpp.skel.md)
- **Referred by:** [settings_view](./settings_view.hpp.skel.md)
- **Referred by:** [keymap_test](../../tests/keymap_test.cpp.skel.md)
- **Referred by:** [text_field](./text_field.hpp.skel.md)

## symbol: InputEvent

`std::variant<KeyEvent, PasteEvent{std::string bytes, bool more}>`, `more` set when more of the same paste follows. A paste's `bytes` are the terminal's bytes unchanged: most terminals send each newline of a paste as a bare CR. The decoder does not know the document, so line breaks are converted by the receiver ([Editor.paste_text](../edit/editor.hpp.skel.md#function-paste_text)).

- **Access:** public.

## function: to_utf8

- **Inputs:** `cp`: a code point.
- **Returns:** its UTF-8 bytes; U+FFFD for a surrogate or a value above U+10FFFF.
- **State changes:** none.
- **Access:** App (inserting a typed `Char`) and Prompt (editing a field).
- **Referred by:** [keymap (implementation)](../app/keymap.cpp.skel.md)
- **Referred by:** [keymap_view (implementation)](./keymap_view.cpp.skel.md)

## class: InputDecoder

- **Inputs:** none.
- **State changes:** `ground → esc → csi/ss3 → ground`, plus `paste`. Invariant: bytes are never lost across `feed` calls. Partial UTF-8 sequences and escape sequences are carried over to the next call.
- **Owns:** buffers.
- **Access:** App.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [input (implementation)](./input.cpp.skel.md)
- **Referred by:** [input_test](../../tests/input_test.cpp.skel.md)
- **Referred by:** [fuzz_input](../../fuzz/fuzz_input.cpp.skel.md)

### function: feed

- **Inputs:** `bytes`: what [Terminal.read_input](../platform/terminal.hpp.skel.md#function-read_input) returned.
- **Returns:** the `InputEvent`s decoded so far.
- **State changes:** advances the state machine.
- **Access:** App, after `wait` reports input.

### function: pending_timeout

- **Inputs:** none.
- **Returns:** `std::optional<ms>`: how long after the last input to call `timeout`: `kEscTimeout` for a partial sequence, `kPasteIdle` inside a paste or a terminal string.
- **State changes:** none.
- **Access:** App, to compute the wait timeout.

### function: timeout

- **Inputs:** none.
- **Returns:** events produced by resolving the pending state, such as a lone `Escape`.
- **State changes:** returns to `ground`.
- **Access:** App, once that long has passed since the terminal last sent bytes (not merely when a wait timed out for another deadline).
