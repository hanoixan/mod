---
role: product
stamp: source e4c306fc, stand-in 79762fa5
---
# module: terminal_output

How mod talks to the terminal: every escape sequence mod writes comes from a **TerminalOutput**, one implementation per terminal behavior, so the rest of the code never spells an escape code. There are two behaviors:

| Mode | Output |
|---|---|
| `xterm` | The modern subset xterm-256color terminals share: the alternate screen (`?1049`), bracketed paste (`?2004`), cursor hiding (`?25`), synchronized output (`?2026`), autowrap control (`?7`), SGR bold, dim, italic, underline, reverse and strike, and the 16 ANSI colors (30–37, 90–97, 40–47, 100–107). mod's colors stay within those 16, so this is exactly the output of earlier versions. |
| `vt100` | Strict VT100: cursor positioning (`ESC[r;cH`), erase (`ESC[2J`), autowrap control (`?7`), and SGR 0, 1, 4, 5 and 7 (normal, bold, underline, blink, reverse) only. No color, no alternate screen, no bracketed paste, no cursor hiding, no synchronized output. On leaving, the screen is cleared and the cursor homed, since there is no alternate screen to return from. |

The `terminal_mode` [setting](../app/settings.hpp.skel.md#function-setting_specs) chooses `auto` (the default), `vt100` or `xterm`. **Auto-detection**, in order:

1. The environment. `TERM` naming a VT-class terminal (`vt52`, `vt100`, `vt102`, `vt220` and other `vt` names, `dumb`) means vt100: the user or system said so. Otherwise any of these means xterm: `TERM` starting with `xterm`, `screen`, `tmux`, `rxvt`, `alacritty`, `kitty`, `foot`, `wezterm`, `konsole`, `gnome`, `st-`, `putty`, `iterm`, `contour`, or equal to `linux`; `COLORTERM` set; `TERM_PROGRAM` set; `WT_SESSION` set.
2. Otherwise a **Primary Device Attributes** query (`ESC[c`), answered within 100 ms. A reply `ESC[?<class>;…c` with class 1, 2, 4 or 6 (VT100, VT100 with options, VT132, VT102) means vt100; class 60 or higher (VT220 and later, which every modern terminal claims) means xterm.
3. No answer means xterm.

Detection runs once at startup; a change of the setting applies the next time mod starts.

- **Owns:** nothing.
- **Access:** public. Pure functions and two stateless implementations.
- **Required:** always.
- **Failure modes:** a terminal that misreports itself in `TERM` gets the wrong mode; the setting overrides detection. Input that arrives during the query is kept and delivered as ordinary input afterwards. A VT100 shows UTF-8 text (rules, check marks) as the terminal's own rendering of those bytes: the mode governs escape codes, not the character set.
- **Depends on:** [Attr](../ui/screen.hpp.skel.md#symbol-attr)
- **Unknowns:** none

## symbol: TerminalMode

`enum class TerminalMode { vt100, xterm }`. The setting adds `auto`, which resolves to one of them.

- **Access:** public.

## class: TerminalOutput

The sequences for one behavior. Implementations are immutable singletons, so the strings they return live for the whole process and are safe to write from a signal handler.

- **Inputs:** none.
- **State changes:** none.
- **Owns:** nothing.
- **Access:** [Screen](../ui/screen.hpp.skel.md#class-screen), [Terminal](./terminal.hpp.skel.md#class-terminal), App.
- **Referred by:** [terminal_output_test](../../tests/terminal_output_test.cpp.skel.md)
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [terminal](./terminal.hpp.skel.md)
- **Referred by:** [terminal_output (implementation)](./terminal_output.cpp.skel.md)
- **Referred by:** [screen](../ui/screen.hpp.skel.md)

### function: output_for

- **Inputs:** a `TerminalMode`.
- **Returns:** `const TerminalOutput&` for it.
- **State changes:** none.
- **Access:** App, tests.

### function: mode

- **Inputs:** none.
- **Returns:** the `TerminalMode` it implements.
- **State changes:** none.
- **Access:** App.

### function: enter

- **Inputs:** none.
- **Returns:** the `std::string_view` written when mod takes over the screen (xterm: `ESC[22;0t ESC[?1049h ESC[?2004h ESC[?25l`, the first pushing the terminal's title on its stack; vt100: empty). `leave()` is written when it gives the screen back (xterm: `ESC[?2004l ESC[?1049l ESC[?25h ESC[0m ESC[0 q ESC[23;0t`, the last popping the title, so the terminal's own comes back; terminals without a title stack ignore both; vt100: `ESC[0m ESC[2J ESC[H`).
- **State changes:** none.
- **Access:** Terminal.

### function: begin_frame

- **Inputs:** none.
- **Returns:** the prefix of every frame (xterm: synchronized-output start, cursor hidden, autowrap off; vt100: autowrap off). `end_frame()` the suffix before the cursor is placed (`ESC[0m`, autowrap on), and `end_sync()` the frame's last bytes (xterm: synchronized-output end; vt100: empty). `show_cursor()` is `ESC[?25h` for xterm and empty for vt100.
- **State changes:** none.
- **Access:** Screen.

### function: cursor_shape

- **Inputs:** a `CursorStyle` (`enum class CursorStyle { bar, bar_blink, block, block_blink, underline, underline_blink }`, in the order of the `cursor_style` setting's names).
- **Returns:** xterm: the DECSCUSR code `ESC[n q` (6 steady bar, 5 blinking bar, 2 steady block, 1 blinking block, 4 steady underline, 3 blinking underline); vt100: empty. xterm's `leave()` ends with `ESC[0 q`, giving the terminal its own shape back.
- **State changes:** none.
- **Access:** Screen.

### function: append_title

- **Inputs:** `out`: bytes being built; `title`: the text for the terminal's title.
- **Returns:** nothing.
- **State changes:** xterm: appends OSC 0 (`ESC ] 0 ; <title> BEL`), which sets the window and icon title, the one most terminal apps show on the tab; every C0 control, DEL, C1 control and byte that is not UTF-8 in `title` becomes `?`, so a file name cannot end the sequence and send another. vt100: appends nothing.
- **Access:** App.

### function: append_attr

- **Inputs:** `out`: the frame being built; an `Attr`.
- **Returns:** nothing.
- **State changes:** appends the SGR that sets exactly this look from a reset: xterm as described above; vt100 only bold (1), underline (4) and reverse (7) of the attribute's flags, dropping dim, italic, strike and every color.
- **Access:** Screen.

## function: mode_from_environment

- **Inputs:** `env`: a function from a variable name to its value, or null when unset.
- **Returns:** `std::optional<TerminalMode>`: step 1 above, or nullopt when the environment does not say.
- **State changes:** none.
- **Access:** App, tests.

## function: mode_from_device_attributes

- **Inputs:** the bytes read after the query.
- **Returns:** `std::optional<TerminalMode>` from step 2, and nullopt when the bytes hold no complete reply. The query itself is the constant `kDeviceAttributesQuery`. `find_device_attributes(bytes)` returns the reply's byte range, so App can keep the other bytes as input.
- **State changes:** none.
- **Access:** App, tests.
