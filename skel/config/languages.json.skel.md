---
role: product
stamp: source 5c169b29, stand-in 6e31b7c5
---
# data: languages

The built-in description of each language mod colors: its extensions (or file names), the [syntax](../src/syntax/language_config.hpp.skel.md#symbol-syntaxspec) the syntax layer uses, and the language server that adds colors for names. It is embedded into the binary at build time. A user copy at `<user_config_dir>/languages.json`, which is the [user_config](../infra/storage.iac.skel.md#resource-user_config) resource, overrides entries with the same `id` and can add new ones.

- **Source:** hand-authored.
- **Required:** optional — without entries there is no LSP coloring.
- **Failure modes:** a listed server is not installed, so LSP is disabled for that language with a status message. A wrong `command` is caught the same way.
- **Depends on:** none
- **Depends on:** [SyntaxSpec](../src/syntax/language_config.hpp.skel.md#symbol-syntaxspec)
- **Unknowns:** none. An entry without `maxFileBytes` gets the 4 MiB default cap; an entry with it overrides the cap for that language only.
- **Referred by:** [CMakeLists.txt](../CMakeLists.txt.skel.md)
- **Referred by:** [language_config](../src/syntax/language_config.hpp.skel.md)
- **Referred by:** [syntax_highlighter_test](../tests/syntax_highlighter_test.cpp.skel.md)

## Schema

The file itself is strict JSON, because [Json.parse](../src/syntax/json.hpp.skel.md#function-parse) accepts no comments: the `//` comments below annotate the schema and are not in the file. The values shown are the shipped defaults.

```jsonc
{
  "version": 1,
  "languages": [
    {
      "id": "cpp",                               // LSP languageId, unique key for overrides
      "extensions": [".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh"],
      "command": ["clangd", "--background-index=false"],
      "initializationOptions": {},               // optional, passed verbatim
      "maxFileBytes": 8388608,                   // optional; LSP not started above this many bytes. Default when absent: 4194304 (4 MiB)
      "syntax": {                                // optional; the syntax layer
        "keywords": ["if", "else", "for", "while", "return", "class", "struct", "..."],
        "constants": ["true", "false", "nullptr", "NULL"],
        "lineComments": ["//"],
        "blockComments": [["/*", "*/"]],
        "strings": [{ "open": "\"", "escape": "\\" }, { "open": "'", "escape": "\\", "charLiteral": true }],
        "stringPrefixes": ["L", "u", "U", "u8", "R", "LR", "uR", "UR", "u8R"]
      }
    },
    {
      "id": "cmake",
      "extensions": [".cmake"],
      "fileNames": ["CMakeLists.txt"],           // matched by whole file name
      "syntax": { "keywords": ["if", "endif", "function", "..."], "caseSensitive": false, "lineComments": ["#"] }
                                                 // no "command": colored by the syntax layer only
    },
    { "id": "rust",       "extensions": [".rs"],               "command": ["rust-analyzer"] },
    { "id": "python",     "extensions": [".py", ".pyi"],       "command": ["pyright-langserver", "--stdio"] },
    { "id": "typescript", "extensions": [".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"], "command": ["typescript-language-server", "--stdio"] },
    { "id": "go",         "extensions": [".go"],               "command": ["gopls"] }
  ]
}
```

## Built-in syntax descriptions

Each built-in language carries a `syntax` description: C/C++, Rust, Python, TypeScript/JavaScript and Go (which also have servers), and JSON (`.json`, `.jsonc`), TOML, YAML (`.yaml`, `.yml`), shell (`.sh`, `.bash`, `.zsh`) and CMake (`.cmake`, `CMakeLists.txt`), which have none. Their keyword and constant lists are each language's reserved words and literal constants as its reference defines them; strings, comments and number rules follow the language. Notable choices: Python's triple-quoted strings are multi-line and its string prefixes are `r`, `b`, `f`, `u` and their two-letter combinations in either case; C/C++, Rust and Go mark `'` as `charLiteral`, so Rust lifetimes and C++ digit separators (`1'000`) are not strings; JavaScript's backtick strings are multi-line; shell and YAML comments need a space before `#`; CMake's and YAML's words match without case (`caseSensitive: false`). [syntax_highlighter_test](../tests/syntax_highlighter_test.cpp.skel.md) checks that every built-in description parses and colors a sample line.
