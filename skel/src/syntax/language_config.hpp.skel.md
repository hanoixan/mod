---
role: product
stamp: source b55efed7, stand-in 9627a40f
---
# module: language_config

Maps file extensions to the language-server commands used for syntax coloring. Defaults are compiled in from [config/languages.json](../../config/languages.json.skel.md). A user file at `<user_config_dir>/languages.json` overrides entries by `id`: a user entry replaces the default entry with the same `id` whole (fields it omits take their defaults, not the default entry's values), with one exception: an entry with no `syntax` member keeps the default entry's `syntax`, since it describes the language rather than the server the user chose, and `"syntax": null` turns the syntax layer off. User entries that add new ids are kept too. User entries come first in the merged list, so for an extension claimed by both a user entry and a default, the user entry wins.

- **Owns:** the merged list of language specs.
- **Access:** public. Loaded once by [App](../app/app.hpp.skel.md#class-app) at startup.
- **Required:** optional — without it there is no LSP coloring.
- **Failure modes:** a malformed user file is ignored and the defaults are used, with a status-line message giving the file and offset: `<path>: JSON: <what> at byte <n>; using the defaults` for a syntax error, or `<path>: <what>; using the defaults` for a schema error. A user file that is not a regular file, cannot be read, or is larger than 1 MiB is ignored the same way. A missing directory or file is normal and gives no message. Commands not found on `PATH` are discovered only at spawn, so LSP is disabled for that language with a message.
- **Depends on:** [Json](./json.hpp.skel.md#class-json)
- **Depends on:** [languages.json](../../config/languages.json.skel.md)
- **Unknowns:** none

## symbol: LanguageServerSpec

`{ std::string id; std::vector<std::string> extensions; std::vector<std::string> file_names; std::vector<std::string> command; Json initialization_options; uint64_t max_file_bytes; std::optional<SyntaxSpec> syntax; bool syntax_given; }` (`syntax_given`: the entry has a `syntax` member, possibly null). An entry describes a language: how to color it without a server (`syntax`), the server that adds colors for its names (`command`), or both. `file_names` matches whole file names, such as `CMakeLists.txt`, that no extension identifies.

`max_file_bytes` comes from the entry's `maxFileBytes`. When the entry has none, it is `kDefaultLspMaxFileBytes` = 4 MiB (4 194 304 bytes). A value that is not a positive integer makes the file malformed, which is handled as in the module's failure modes.

The schema checks, each of which makes the whole file malformed: the top level is an object with `"version": 1` and a `languages` array; each entry is an object with a non-empty string `id` that no earlier entry in the same file uses, at least one of `extensions` and `fileNames` (arrays of non-empty strings), at least one of `command` (a non-empty array of non-empty strings) and `syntax` (as [SyntaxSpec](#symbol-syntaxspec) describes, or `null` for none), and an optional `maxFileBytes` as above. `initializationOptions` may be any JSON value (null when absent). Unknown keys are ignored. Extensions are stored lowercase with a leading dot (one is added when missing).

- **Access:** public.
- **Referred by:** [lsp_client](./lsp_client.hpp.skel.md)
- **Referred by:** [lsp_pool](./lsp_pool.hpp.skel.md)

## symbol: SyntaxSpec

The `syntax` member of a language entry: what the [syntax layer](./syntax_highlighter.hpp.skel.md#class-syntaxhighlighter) needs to color the language. Every member is optional.

| JSON member | Field | Meaning |
|---|---|---|
| `keywords` | `std::vector<std::string> keywords` | words drawn as `keyword` |
| `constants` | `constants` | words drawn as `constant` (`true`, `None`, `nullptr`) |
| `caseSensitive` | `bool case_sensitive` (default true) | whether those words match only in their written case |
| `lineComments` | `std::vector<std::string> line_comments` | markers that start a comment running to the line's end |
| `commentNeedsSpace` | `bool comment_needs_space` (default false) | a line-comment marker counts only at the line's start or after a space or tab |
| `blockComments` | `std::vector<std::pair<std::string, std::string>> block_comments` | `[open, close]` pairs |
| `strings` | `std::vector<StringRule> strings` | `{ "open", "close" (default: open), "escape" (one character, optional), "multiline" (default false), "maxLength" (optional), "charLiteral" (default false: a string only when its body is one character, as one UTF-8 sequence, or one escape sequence) }` |
| `stringPrefixes` | `std::vector<std::string> string_prefixes` | words allowed directly before an opening delimiter (`f`, `rb`) |
| `numbers` | `bool numbers` (default true) | color numbers |

A member of the wrong type, an empty marker or delimiter, or an escape longer than one character makes the file malformed, as other schema errors do.

- **Access:** public.
- **Referred by:** [syntax_highlighter](./syntax_highlighter.hpp.skel.md)
- **Referred by:** [languages](../../config/languages.json.skel.md)

## class: LanguageConfig

- **Inputs:** none.
- **State changes:** immutable after `load`.
- **Owns:** specs.
- **Access:** main thread.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [language_config (implementation)](./language_config.cpp.skel.md)
- **Referred by:** [json_test](../../tests/json_test.cpp.skel.md)
- **Referred by:** [syntax_highlighter_test](../../tests/syntax_highlighter_test.cpp.skel.md)
- **Referred by:** [fuzz_config](../../fuzz/fuzz_config.cpp.skel.md)

### function: load

- **Inputs:** ambient: [user_config_dir](../platform/fs.hpp.skel.md#function-user_config_dir) and the embedded defaults. An overload takes `config_dir`: a `std::optional<std::filesystem::path>` (no user file when `nullopt`), so tests need not change the environment; the ambient form calls it with `user_config_dir()`, or `nullopt` when that fails.
- **Returns:** `LanguageConfig::Loaded`: `{ LanguageConfig config; std::optional<std::string> warning; }`.
- **State changes:** reads the user file if it exists.
- **Access:** App startup.
- **Depends on:** [user_config](../../infra/storage.iac.skel.md#resource-user_config)

### function: merge

- **Inputs:** `user_text`: the user file's content, or `nullopt` for none; `user_name`: the name used in the warning (the path).
- **Returns:** `Loaded`: the defaults merged with the user entries, or the defaults alone with a warning when `user_text` is malformed.
- **State changes:** none.
- **Access:** `load`, and tests.

### function: parse

- **Inputs:** `text`: one `languages.json` document.
- **Returns:** `Result<std::vector<LanguageServerSpec>>`; `format` with a message for a syntax or schema error.
- **State changes:** none.
- **Access:** `merge`, and [json_test](../../tests/json_test.cpp.skel.md), which parses the embedded defaults.

### function: default_json

- **Inputs:** none.
- **Returns:** the embedded `config/languages.json` text, as a `std::string_view` over static storage.
- **State changes:** none.
- **Access:** public, for the test that the embedded defaults parse.

### function: find_for_path

- **Inputs:** `path`.
- **Returns:** a `const LanguageServerSpec*` whose `fileNames` holds the path's file name exactly, else whose `extensions` holds its lowercase extension, or null. A path without an extension (including a dot-file such as `.bashrc`) matches only by file name.
- **State changes:** none.
- **Access:** App, when opening a document.

### function: specs

- **Inputs:** none.
- **Returns:** the merged list, user entries first.
- **State changes:** none.
- **Access:** public.

## function: find_project_root

- **Inputs:** `file`: a path to an edited file.
- **Returns:** the folder a language server should treat as the project: the nearest folder, from the file's own folder upwards, that holds any of `.git`, `compile_commands.json`, `Cargo.toml`, `go.mod`, `package.json` or `pyproject.toml`; the file's own folder (made absolute) when none does.
- **State changes:** none; it only checks whether those names exist.
- **Access:** SemanticHighlighter, to choose the shared server; tests.
- **Referred by:** [semantic_highlighter](./semantic_highlighter.hpp.skel.md)
- **Referred by:** [lsp_client_test](../../tests/lsp_client_test.cpp.skel.md)
