---
role: product
stamp: source ffe9be82, stand-in 61e9653d
---
# module: startup

The user's configuration for a session, applied before the first frame, kept apart from App so it can be tested without a terminal.

- **Owns:** nothing.
- **Access:** App's constructor.
- **Required:** always.
- **Failure modes:** a bad settings.json, a bad keymap entry or a bad color entry: the part that is valid applies, and the warning is returned for the status line (see [load_configuration](#function-load_configuration)).
- **Depends on:** [Settings](./settings.hpp.skel.md#class-settings)
- **Depends on:** [Keymap.apply_overrides](./keymap.hpp.skel.md#function-apply_overrides)
- **Depends on:** [ColorTheme](../ui/theme.hpp.skel.md#class-colortheme)
- **Depends on:** [CliOptions](./cli_options.hpp.skel.md#symbol-clioptions)
- **Unknowns:** none

## function: load_configuration

- **Inputs:** `settings` (built on the user's config folder), `keymap`, `theme` (App passes the process's), `options` (the parsed command line).
- **Returns:** the status line to start with: the last warning given, in this order: [Settings.load](./settings.hpp.skel.md#function-load)'s, then the keymap's, then the colors' (each summarized by [summarize_warnings](#function-summarize_warnings)), as each would replace the one before; nullopt when there was none.
- **State changes:** loads `settings`; applies the command line's settings as session overrides (never written); applies settings.json's `keymap` to `keymap`; sets `theme`'s darkness from the settings; applies settings.json's `colors` to `theme`; then the command line's colors as session colors (never saved).
- **Access:** App's constructor, which then gives the menu the recent settings and the screen the cursor style.
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [startup (implementation)](./startup.cpp.skel.md)
- **Referred by:** [startup_test](../../tests/startup_test.cpp.skel.md)

## function: summarize_warnings

- **Inputs:** `what`: the part, such as "settings.json keymap"; `warnings`.
- **Returns:** "<what>: <first warning>", followed by " (and N more)" when there are N others; nullopt when there are none.
- **State changes:** none.
- **Access:** load_configuration.
