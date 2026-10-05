---
role: product
stamp: source a6ca2e42, stand-in 9d8e0d0b
---
# module: cli_options

The command line, parsed into what [App](./app.hpp.skel.md#class-app) starts with. Pure: the arguments in, a `CliOptions` or a message out, so every form is tested without a terminal.

| Flag | Meaning |
|---|---|
| `PATH...` | files to open, each as a document |
| `--` | ends the options; what follows is a path even if it starts with `-` |
| `-h`, `--help` | print the usage and exit |
| `--version` | print `mod <version>` and exit |
| `-ro`, `--read-only` | open the named files in [read-only mode](../../docs/manual/read-only.md.skel.md) |
| `--persist-history` | turn Persist History on for the named files, creating their `.history` sidecars |
| `--<setting>=<value>` | a session value for a scalar [setting](./settings.hpp.skel.md#function-setting_specs), never saved: the key with dashes for underscores (`--tab-width=8`, `--darkness=night`); a choice's value may use dashes for its spaces (`--read-only-copy=visible-text`) |
| `--<boolean>`, `--no-<boolean>` | a boolean setting on or off; `=on`, `off`, `true`, `false`, `yes`, `no`, `1` and `0` also work |
| `--color <name>=<spec>`, `--color=<name>=<spec>` | a session color, never saved; repeatable ([color specs](../ui/theme.hpp.skel.md#function-parse_color_spec)) |

- **Owns:** nothing.
- **Access:** public. Pure functions.
- **Required:** always.
- **Failure modes:** an unknown flag, a missing or bad value, or an unknown color name: the result is the message (`unknown option --x`, `--tab-width wants a number from 1 to 16`, `--darkness wants night, normal or paper`, …), which main prints with the usage before exiting with status 2.
- **Depends on:** [setting_specs](./settings.hpp.skel.md#function-setting_specs)
- **Depends on:** [ColorTheme](../ui/theme.hpp.skel.md#class-colortheme)
- **Depends on:** [path_from_user](../platform/path_text.hpp.skel.md#function-path_from_user)
- **Unknowns:** none

## symbol: CliOptions

`{ Action action; std::vector<std::filesystem::path> paths; bool read_only; bool persist_history; std::vector<std::pair<std::string, std::int64_t>> settings; std::vector<std::pair<std::string, std::string>> colors; }` with `enum class Action { run, help, version }`. `settings` holds schema keys and validated values, in command-line order (a later flag for the same key wins); `colors` holds names and specs, validated.

- **Access:** public.
- **Referred by:** [App.handle_external_change](./app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [startup](./startup.hpp.skel.md)

## function: parse_cli

- **Inputs:** `args`: the arguments after the program name.
- **Returns:** `std::expected<CliOptions, std::string>`: the options, or the message for the first bad argument. File arguments go through [path_from_user](../platform/path_text.hpp.skel.md#function-path_from_user), so on Windows `C:\notes.md` works; `-` stays `-`.
- **State changes:** none.
- **Access:** main, tests.
- **Referred by:** [cli_options (implementation)](./cli_options.cpp.skel.md)
- **Referred by:** [main](../main.cpp.skel.md)
- **Referred by:** [cli_options_test](../../tests/cli_options_test.cpp.skel.md)
- **Referred by:** [command-line.md](../../docs/manual/command-line.md.skel.md)
- **Referred by:** [fuzz_cli](../../fuzz/fuzz_cli.cpp.skel.md)

## function: cli_usage

- **Inputs:** none.
- **Returns:** the usage text: the synopsis, the flags above, and every scalar setting's flag with its values (a number's range, a choice's names, `on`/`off`) and default, generated from the schema so it never disagrees with it.
- **State changes:** none.
- **Access:** main, tests.
- **Referred by:** [main](../main.cpp.skel.md)
- **Referred by:** [cli_options_test](../../tests/cli_options_test.cpp.skel.md)
