# Syntax coloring

Markdown is styled by mod itself, whenever View > Syntax Coloring is on. Other languages are colored in two layers:

1. **Syntax coloring**, built into mod: keywords, strings, comments, numbers and named constants such as `true` and `None`. It needs nothing installed and works on files of any size.
2. **A language server**, a separate program that understands the language and tells mod what each name is: a type, a function, a parameter, a variable. Its colors are drawn over the first layer. mod uses only this coloring, called semantic tokens; it does not offer completion or diagnostics.

Without a server, or while it starts, the first layer still colors the file. If a server is named but not installed, the status line says `LSP off` with the reason. View > Syntax Coloring turns both layers off for the document you are in; the `syntax_coloring` [setting](settings.md) chooses it for each document as it is opened, and changing it sets every open document. How each kind of text looks is set in [Colors](colors.md).

## Built-in languages

| Language | Files | Server |
|---|---|---|
| C and C++ | `.c` `.h` `.cc` `.cpp` `.cxx` `.hpp` `.hh` | `clangd --background-index=false` |
| Rust | `.rs` | `rust-analyzer` |
| Python | `.py` `.pyi` | `pyright-langserver --stdio` |
| TypeScript, JavaScript | `.ts` `.tsx` `.js` `.jsx` `.mjs` `.cjs` | `typescript-language-server --stdio` |
| Go | `.go` | `gopls` |
| JSON | `.json` `.jsonc` | none |
| TOML | `.toml` | none |
| YAML | `.yaml` `.yml` | none |
| Shell | `.sh` `.bash` `.zsh` | none |
| CMake | `.cmake`, `CMakeLists.txt` | none |

A server must offer semantic tokens for mod to use it; one that does not is stopped with `LSP off: … has no semantic tokens`. Plain pyright is one of these: it gives no colors, so for Python install basedpyright (below) and name it in your own list.

## Your own list

Create `languages.json` in your configuration folder (`~/.config/mod/` or `$XDG_CONFIG_HOME/mod/`). An entry with the same `id` as a built-in one replaces it; new ids add languages. An entry that says nothing about `syntax` keeps the built-in syntax coloring for its language, so to use basedpyright for Python this is enough:

```json
{
  "version": 1,
  "languages": [
    { "id": "python", "extensions": [".py", ".pyi"], "command": ["basedpyright-langserver", "--stdio"] }
  ]
}
```

Each entry has:

- `id`: the language's name, sent to the server.
- `extensions`: the file extensions it handles.
- `fileNames`: whole file names, such as `CMakeLists.txt`, matched exactly and before any extension. An entry needs `extensions`, `fileNames` or both.
- `command` (optional): the server program and its arguments; the program is looked up on your `PATH`. Without one, no server is started.
- `syntax` (optional): the syntax coloring, described below. `null` turns it off. An entry needs a `command` or a `syntax`.
- `initializationOptions` (optional): passed to the server as they are.
- `maxFileBytes` (optional): files larger than this many bytes are not sent to the server, because it needs the whole text; 4 MiB unless set (8 MiB for C and C++). Syntax coloring has no limit.

## Describing a language

The `syntax` object tells mod what to color. Every member is optional.

| Member | Meaning |
|---|---|
| `keywords` | Words colored as `keyword` |
| `constants` | Words colored as `constant`, such as `true` and `nil` |
| `caseSensitive` | `false` to match those words in any case (default `true`) |
| `lineComments` | Markers that start a comment running to the end of the line, such as `"//"` or `"#"` |
| `commentNeedsSpace` | `true` when a line comment marker counts only at the start of a line or after a space or tab, as in shell, where `$#` is not a comment |
| `blockComments` | `[open, close]` pairs, such as `["/*", "*/"]`; these may span lines |
| `strings` | The kinds of string, each `{ "open": "\"", "close": "\"", "escape": "\\", "multiline": false }`. `close` defaults to `open`. `escape` is the one character that escapes the next. `multiline` lets a string run on past its line. With `"charLiteral": true`, a quote is a string only around one character or one escape such as `'\n'`, which keeps Rust's `'a` lifetimes and C++'s `1'000` plain. With `"maxLength": n`, a quote not closed within n bytes is not a string. |
| `stringPrefixes` | Words allowed directly before a string, such as Python's `f` and `rb` |
| `numbers` | `false` to leave numbers plain (default `true`) |

When several markers could start at the same place, the longest wins, so a `"""` string is found before a `"` one. For example, a language with Lua's comments and strings:

```json
{
  "version": 1,
  "languages": [
    {
      "id": "lua",
      "extensions": [".lua"],
      "syntax": {
        "keywords": ["and", "break", "do", "else", "elseif", "end", "for", "function", "if", "in", "local", "not", "or", "repeat", "return", "then", "until", "while"],
        "constants": ["true", "false", "nil"],
        "lineComments": ["--"],
        "blockComments": [["--[[", "]]"]],
        "strings": [{ "open": "\"", "escape": "\\" }, { "open": "'", "escape": "\\" }, { "open": "[[", "close": "]]", "multiline": true }]
      }
    }
  ]
}
```

Comments and multi-line strings are followed from the start of the file. In a very large file, mod looks back at most 4 MiB from the nearest point it already knows, so a comment opened further back than that may be colored as code until you scroll up to it.

## Sharing

Documents of the same language in the same project share one server. The project is the nearest folder, from the file's own upwards, that holds `.git`, `compile_commands.json`, `Cargo.toml`, `go.mod`, `package.json` or `pyproject.toml`; a file with none of those above it is its own project. The server stops when the last of its documents is closed. If a server crashes, mod restarts it, up to three times.

## Installing a server

Install servers the way their projects describe, so that the command is on your `PATH`. For example, basedpyright installs with Python's `uv` and brings its own Node.js:

```bash
uv tool install basedpyright
```
